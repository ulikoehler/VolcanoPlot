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
    Point2D verts[4];
    float depth;
    Color color;
};

Point2D project(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
}

float projectDepth(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipZ = vp[8]*x + vp[9]*y + vp[10]*z + vp[11];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return 0.0f;
    return clipZ / clipW;
}

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

    if (filled_.empty()) return;

    auto vp = camera_.viewProjection();

    auto corner = [&](int ix, int iy, int iz, int idx) -> Point3D {
        return {
            static_cast<float>(ix) + (idx & 1 ? 1.0f : 0.0f),
            static_cast<float>(iy) + (idx & 2 ? 1.0f : 0.0f),
            static_cast<float>(iz) + (idx & 4 ? 1.0f : 0.0f)
        };
    };

    auto voxelColor = [&](size_t linearIdx) -> Color {
        if (linearIdx < config_.colors.size())
            return config_.colors[linearIdx];
        return config_.color;
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
                        pf.verts[v] = project(vp, c.x, c.y, c.z);
                        avgDepth += projectDepth(vp, c.x, c.y, c.z);
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
        Point2D v0 = pf.verts[0], v1 = pf.verts[1];
        Point2D v2 = pf.verts[2], v3 = pf.verts[3];
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v1);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v3);
        for (int k = 0; k < 6; ++k) fillColors_.push_back(pf.color);
    }

    // Build edge segments.
    if (config_.drawEdges) {
        for (uint32_t ix = 0; ix < nx_; ++ix)
            for (uint32_t iy = 0; iy < ny_; ++iy)
                for (uint32_t iz = 0; iz < nz_; ++iz) {
                    size_t idx = static_cast<size_t>(ix) * ny_ * nz_ +
                                 static_cast<size_t>(iy) * nz_ + iz;
                    if (!filled_[idx]) continue;
                    for (int e = 0; e < 12; ++e) {
                        Point3D c0 = corner(ix, iy, iz, edges[e][0]);
                        Point3D c1 = corner(ix, iy, iz, edges[e][1]);
                        edgeSegments_.push_back(project(vp, c0.x, c0.y, c0.z));
                        edgeSegments_.push_back(project(vp, c1.x, c1.y, c1.z));
                    }
                }
    }
}

void VoxelsPlot::prepare(render::Renderer& r) {
    projectVoxels();

    auto& ctx = r.backend().context();

    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }

    if (config_.drawEdges && !edgeSegments_.empty()) {
        edgeRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        edgeRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{edgeSegments_}, config_.edgeColor,
                             config_.edgeWidth);
    }

    prepared_ = true;
}

void VoxelsPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!fillPositions_.empty())
        fillRenderer_.draw(cmd, vrect, t);

    if (config_.drawEdges && !edgeSegments_.empty())
        edgeRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(edgeSegments_.size()));
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
