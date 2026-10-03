// volcano/plot/plots/Bar3D.cpp — 3D bar chart implementation
#include "volcano/plot/plots/Bar3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

struct ProjectedFace {
    /// Raw world-space corners — the renderer projects them
    /// (vertex shader on capable backends, CPU otherwise).
    Point3D verts[4];
    float depth;
    /// Face orientation index (0..5) — keys the shade table.
    int face;
};

} // namespace

Bar3D::Bar3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
             std::vector<float> dx, std::vector<float> dy, std::vector<float> dz,
             Bar3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      dx_(std::move(dx)), dy_(std::move(dy)), dz_(std::move(dz)),
      config_(std::move(config)) {
    size_t n = x_.size();
    if (y_.size() != n || z_.size() != n || dx_.size() != n ||
        dy_.size() != n || dz_.size() != n)
        throw std::invalid_argument("Bar3D: all arrays must have the same size");
}

void Bar3D::buildEdges() {
    edgePoints_.clear();
    if (!config_.drawEdges) return;
    static const int edges[12][2] = {
        {0,1}, {1,3}, {3,2}, {2,0},  // bottom face
        {4,5}, {5,7}, {7,6}, {6,4},  // top face
        {0,4}, {1,5}, {3,7}, {2,6},  // vertical edges
    };
    for (size_t bar = 0; bar < x_.size(); ++bar)
        for (int e = 0; e < 12; ++e) {
            edgePoints_.push_back(corner(bar, edges[e][0]));
            edgePoints_.push_back(corner(bar, edges[e][1]));
        }
}

void Bar3D::projectBars() {
    fillPositions_.clear();
    fillColors_.clear();
    boxes_.clear();
    boxesReady_ = false;

    if (x_.empty()) return;
    vp_ = camera_.viewProjection();

    // Instanced `DrawBoxes3D` offload: the renderer consumes the raw
    // per-box records and the vertex shader expands the unit cube —
    // no face expansion or painter's sort on the CPU. Face shading is
    // applied per-orientation in the shader (the same table the
    // expansion path uses).
    if (render::OffloadConfig::allowGpu(
            render::OffloadConfig::global().instancing)) {
        boxes_.reserve(x_.size());
        for (size_t bar = 0; bar < x_.size(); ++bar)
            boxes_.push_back({x_[bar], y_[bar], z_[bar], 0.0f,
                              dx_[bar], dy_[bar], dz_[bar], 0.0f,
                              config_.color});
        boxesReady_ = true;
        buildEdges();
        return;
    }
    expandFaces();
    buildEdges();
}

void Bar3D::expandFaces() {
    const auto& vp = vp_;

    // 6 faces, each defined by 4 corner indices (CCW when viewed from outside).
    static const int faceCorners[6][4] = {
        {0, 2, 3, 1},  // -X face (x = x0)
        {4, 5, 7, 6},  // +X face (x = x1)
        {0, 1, 5, 4},  // -Y face (y = y0)
        {2, 6, 7, 3},  // +Y face (y = y1)
        {0, 4, 6, 2},  // -Z face (z = z0, bottom)
        {1, 3, 7, 5},  // +Z face (z = z1, top)
    };

    // Shade factor per face orientation for simple lighting effect.
    static const float faceShade[6] = {0.7f, 0.85f, 0.6f, 0.8f, 0.5f, 1.0f};

    std::vector<ProjectedFace> faces;

    for (size_t bar = 0; bar < x_.size(); ++bar) {
        for (int f = 0; f < 6; ++f) {
            ProjectedFace pf;
            pf.face = f;
            float avgDepth = 0.0f;
            for (int v = 0; v < 4; ++v) {
                Point3D c = corner(bar, faceCorners[f][v]);
                pf.verts[v] = c;
                avgDepth += projectDepth3D(vp, c);
            }
            pf.depth = avgDepth / 4.0f;
            faces.push_back(pf);
        }
    }

    // Sort faces back-to-front (painter's algorithm).
    std::sort(faces.begin(), faces.end(),
              [](const ProjectedFace& a, const ProjectedFace& b) {
                  return a.depth > b.depth;  // larger depth = farther away
              });

    // Build fill triangles (2 per face).
    for (const auto& pf : faces) {
        float shade = faceShade[pf.face];
        Color faceColor = {
            config_.color.r * shade,
            config_.color.g * shade,
            config_.color.b * shade,
            config_.color.a
        };

        Point3D v0 = pf.verts[0], v1 = pf.verts[1];
        Point3D v2 = pf.verts[2], v3 = pf.verts[3];

        fillPositions_.push_back(v0);
        fillPositions_.push_back(v1);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v3);
        for (int k = 0; k < 6; ++k) fillColors_.push_back(faceColor);
    }
}

void Bar3D::prepare(render::Renderer& r) {
    projectBars();

    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    // Instanced path consumed the box records directly.
    if (boxesReady_ && fillRenderer_->uploadBoxes3D(std::span{boxes_},
                                                  vp_)) {
        instanced_ = true;
    } else {
        instanced_ = false;
        // A backend that declines instancing still gets the expanded
        // soup — built now if the instancing branch skipped it.
        if (fillPositions_.empty() && !x_.empty()) {
            expandFaces();
        }
        if (!fillPositions_.empty())
            fillRenderer_->upload3D(std::span{fillPositions_},
                                    std::span{fillColors_}, vp_);
    }

    if (config_.drawEdges && !edgePoints_.empty()) {
        if (!lineRenderer_) lineRenderer_ = r.gpu().createLineSegmentRenderer();
        lineRenderer_->upload3D(std::span{edgePoints_}, config_.edgeColor,
                                config_.edgeWidth, vp_);
    }

    prepared_ = true;
}

void Bar3D::draw(render::Cmd& cmd, render::Renderer& r,
                 const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    if (instanced_ || !fillPositions_.empty())
        fillRenderer_->draw(cmd, vrect, t);

    if (config_.drawEdges && edgePoints_.size() >= 2)
        lineRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(edgePoints_.size()));
}

void Bar3D::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i] + dx_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i] + dy_[i]);
        v.z.min = std::min(v.z.min, z_[i]);
        v.z.max = std::max(v.z.max, z_[i] + dz_[i]);
    }
}

} // namespace volcano::plot
