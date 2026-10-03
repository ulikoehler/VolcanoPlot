// volcano/plot/plots/VoxelsPlot.cpp — 3D voxel plot implementation
#include "volcano/plot/plots/VoxelsPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

struct ProjectedFace {
    /// Raw world-space corners — projected by the renderer (vertex
    /// shader on capable backends, CPU otherwise).
    Point3D verts[4];
    float depth;
    Color color;
};

// 6 faces, each defined by 4 corner indices (CCW when viewed from outside).
// Corner index: bit 0 = +x, bit 1 = +y, bit 2 = +z.
static const int faceCorners[6][4] = {
    {0, 2, 3, 1},  // -X face
    {4, 5, 7, 6},  // +X face
    {0, 1, 5, 4},  // -Y face
    {2, 6, 7, 3},  // +Y face
    {0, 4, 6, 2},  // -Z face (bottom)
    {1, 3, 7, 5},  // +Z face (top)
};

static const float faceShade[6] = {0.7f, 0.85f, 0.6f, 0.8f, 0.5f, 1.0f};

// 12 edges of a cube.
static const int edges[12][2] = {
    {0,1}, {1,3}, {3,2}, {2,0},
    {4,5}, {5,7}, {7,6}, {6,4},
    {0,4}, {1,5}, {3,7}, {2,6},
};

} // namespace

VoxelsPlot::VoxelsPlot(std::vector<uint8_t> filled, uint32_t nx, uint32_t ny,
                       uint32_t nz, VoxelsConfig config)
    : filled_(std::move(filled)), nx_(nx), ny_(ny), nz_(nz),
      config_(std::move(config)) {
    if (filled_.size() != nx_ * ny_ * nz_)
        throw std::invalid_argument("VoxelsPlot: filled size must match nx*ny*nz");
}

void VoxelsPlot::projectVoxels() {
    fillPositions_.clear();
    fillColors_.clear();
    edgeSegments_.clear();
    boxes_.clear();
    boxesReady_ = false;

    if (filled_.empty()) return;

    vp_ = camera_.viewProjection();
    const auto& vp = vp_;
    (void)vp;

    auto voxelColor = [&](size_t linearIdx) -> Color {
        if (linearIdx < config_.colors.size())
            return config_.colors[linearIdx];
        return config_.color;
    };

    // Instanced `DrawBoxes3D` offload: per-voxel instance records, the
    // vertex shader expands the unit cube — no face expansion or
    // painter's sort on the CPU. Per-face shading runs in the shader.
    if (render::OffloadConfig::allowGpu(
            render::OffloadConfig::global().instancing)) {
        for (uint32_t ix = 0; ix < nx_; ++ix)
            for (uint32_t iy = 0; iy < ny_; ++iy)
                for (uint32_t iz = 0; iz < nz_; ++iz) {
                    size_t idx = static_cast<size_t>(ix) * ny_ * nz_ +
                                 static_cast<size_t>(iy) * nz_ + iz;
                    if (!filled_[idx]) continue;
                    boxes_.push_back({float(ix), float(iy), float(iz),
                                      0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                                      voxelColor(idx)});
                }
        boxesReady_ = true;
        buildEdges();
        return;
    }

    expandFaces();
    buildEdges();
}

/// Painter's-algorithm face expansion — the CPU fallback for backends
/// without uploadBoxes3DDevice (native Vulkan, Canvas2D).
void VoxelsPlot::expandFaces() {
    auto voxelColor = [&](size_t linearIdx) -> Color {
        if (linearIdx < config_.colors.size())
            return config_.colors[linearIdx];
        return config_.color;
    };
    const auto& vp = vp_;

    auto corner = [&](int ix, int iy, int iz, int idx) -> Point3D {
        return {
            static_cast<float>(ix) + (idx & 1 ? 1.0f : 0.0f),
            static_cast<float>(iy) + (idx & 2 ? 1.0f : 0.0f),
            static_cast<float>(iz) + (idx & 4 ? 1.0f : 0.0f)
        };
    };

    std::vector<ProjectedFace> faces;

    for (uint32_t ix = 0; ix < nx_; ++ix)
        for (uint32_t iy = 0; iy < ny_; ++iy)
            for (uint32_t iz = 0; iz < nz_; ++iz) {
                size_t idx = static_cast<size_t>(ix) * ny_ * nz_ +
                             static_cast<size_t>(iy) * nz_ + iz;
                if (!filled_[idx]) continue;

                Color baseColor = voxelColor(idx);

                for (int f = 0; f < 6; ++f) {
                    ProjectedFace pf;
                    float avgDepth = 0.0f;
                    for (int v = 0; v < 4; ++v) {
                        Point3D c = corner(ix, iy, iz, faceCorners[f][v]);
                        pf.verts[v] = c;
                        avgDepth += projectDepth3D(vp, c);
                    }
                    pf.depth = avgDepth / 4.0f;
                    float shade = faceShade[f];
                    pf.color = {
                        baseColor.r * shade,
                        baseColor.g * shade,
                        baseColor.b * shade,
                        baseColor.a
                    };
                    faces.push_back(pf);
                }
            }

    // Sort faces back-to-front (painter's algorithm).
    std::sort(faces.begin(), faces.end(),
              [](const ProjectedFace& a, const ProjectedFace& b) {
                  return a.depth > b.depth;
              });

    // Build fill triangles (2 per face).
    for (const auto& pf : faces) {
        Point3D v0 = pf.verts[0], v1 = pf.verts[1];
        Point3D v2 = pf.verts[2], v3 = pf.verts[3];
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v1);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v3);
        for (int k = 0; k < 6; ++k) fillColors_.push_back(pf.color);
    }
}

void VoxelsPlot::buildEdges() {
    edgeSegments_.clear();
    if (!config_.drawEdges) return;
    for (uint32_t ix = 0; ix < nx_; ++ix)
        for (uint32_t iy = 0; iy < ny_; ++iy)
            for (uint32_t iz = 0; iz < nz_; ++iz) {
                size_t idx = static_cast<size_t>(ix) * ny_ * nz_ +
                             static_cast<size_t>(iy) * nz_ + iz;
                if (!filled_[idx]) continue;
                for (int e = 0; e < 12; ++e) {
                    edgeSegments_.push_back(
                        {float(ix) + (edges[e][0] & 1 ? 1.0f : 0.0f),
                         float(iy) + (edges[e][0] & 2 ? 1.0f : 0.0f),
                         float(iz) + (edges[e][0] & 4 ? 1.0f : 0.0f)});
                    edgeSegments_.push_back(
                        {float(ix) + (edges[e][1] & 1 ? 1.0f : 0.0f),
                         float(iy) + (edges[e][1] & 2 ? 1.0f : 0.0f),
                         float(iz) + (edges[e][1] & 4 ? 1.0f : 0.0f)});
                }
            }
}

void VoxelsPlot::prepare(render::Renderer& r) {
    projectVoxels();


    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    instanced_ = boxesReady_ &&
                 fillRenderer_->uploadBoxes3D(std::span{boxes_}, vp_);
    if (!instanced_ && boxesReady_ && fillPositions_.empty())
        expandFaces();  // backend declined — paint the expanded soup
    if (!instanced_ && !fillPositions_.empty()) {
        fillRenderer_->upload3D(std::span{fillPositions_},
                                std::span{fillColors_}, vp_);
    }

    if (config_.drawEdges && !edgeSegments_.empty()) {
        if (!edgeRenderer_) edgeRenderer_ = r.gpu().createLineSegmentRenderer();
        edgeRenderer_->upload3D(std::span{edgeSegments_},
                                config_.edgeColor,
                                config_.edgeWidth, vp_);
    }

    prepared_ = true;
}

void VoxelsPlot::draw(render::Cmd& cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    if (instanced_ || !fillPositions_.empty())
        fillRenderer_->draw(cmd, vrect, t);

    if (config_.drawEdges && !edgeSegments_.empty())
        edgeRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(edgeSegments_.size()));
}

void VoxelsPlot::contributeToAutoscale(Viewport& v) const {
    // Only contribute if there are filled voxels.
    bool any = false;
    for (uint8_t f : filled_) {
        if (f) { any = true; break; }
    }
    if (!any) return;

    v.x.min = std::min(v.x.min, 0.0f);
    v.x.max = std::max(v.x.max, static_cast<float>(nx_));
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, static_cast<float>(ny_));
    v.z.min = std::min(v.z.min, 0.0f);
    v.z.max = std::max(v.z.max, static_cast<float>(nz_));
}

} // namespace volcano::plot
