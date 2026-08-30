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
    Point2D verts[4];
    float depth;
};

// Project a 3D point through a 4x4 row-major matrix to 2D NDC.
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

void Bar3D::projectBars() {
    fillPositions_.clear();
    fillColors_.clear();
    edgePoints_.clear();

    if (x_.empty()) return;

    auto vp = camera_.viewProjection();

    // 8 corners of a box: (x, y, z) + (dx, dy, dz) combinations.
    // Index: bit 0 = dx, bit 1 = dy, bit 2 = dz.
    auto corner = [&](size_t barIdx, int idx) -> Point3D {
        return {
            x_[barIdx] + (idx & 1 ? dx_[barIdx] : 0.0f),
            y_[barIdx] + (idx & 2 ? dy_[barIdx] : 0.0f),
            z_[barIdx] + (idx & 4 ? dz_[barIdx] : 0.0f)
        };
    };

    // 6 faces, each defined by 4 corner indices (CCW when viewed from outside).
    static const int faceCorners[6][4] = {
        {0, 2, 3, 1},  // -X face (x = x0)
        {4, 5, 7, 6},  // +X face (x = x1)
        {0, 1, 5, 4},  // -Y face (y = y0)
        {2, 6, 7, 3},  // +Y face (y = y1)
        {0, 4, 6, 2},  // -Z face (z = z0, bottom)
        {1, 3, 7, 5},  // +Z face (z = z1, top)
    };

    // Shade factor per face for simple lighting effect.
    static const float faceShade[6] = {0.7f, 0.85f, 0.6f, 0.8f, 0.5f, 1.0f};

    std::vector<ProjectedFace> faces;

    for (size_t bar = 0; bar < x_.size(); ++bar) {
        for (int f = 0; f < 6; ++f) {
            ProjectedFace pf;
            float avgDepth = 0.0f;
            for (int v = 0; v < 4; ++v) {
                Point3D c = corner(bar, faceCorners[f][v]);
                pf.verts[v] = project(vp, c.x, c.y, c.z);
                avgDepth += projectDepth(vp, c.x, c.y, c.z);
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

    // Build fill triangles (2 per face) and edge line segments.
    for (size_t i = 0; i < faces.size(); ++i) {
        const auto& pf = faces[i];
        int faceIdx = static_cast<int>(i) % 6;
        float shade = faceShade[faceIdx];
        Color faceColor = {
            config_.color.r * shade,
            config_.color.g * shade,
            config_.color.b * shade,
            config_.color.a
        };

        // Two triangles: (v0, v1, v2) and (v0, v2, v3).
        Point2D v0 = pf.verts[0], v1 = pf.verts[1];
        Point2D v2 = pf.verts[2], v3 = pf.verts[3];

        fillPositions_.push_back(v0);
        fillPositions_.push_back(v1);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v0);
        fillPositions_.push_back(v2);
        fillPositions_.push_back(v3);
        for (int k = 0; k < 6; ++k) fillColors_.push_back(faceColor);
    }

    // Build edges for all bars (not sorted — edges drawn on top).
    if (config_.drawEdges) {
        for (size_t bar = 0; bar < x_.size(); ++bar) {
            // 12 edges of the box.
            static const int edges[12][2] = {
                {0,1}, {1,3}, {3,2}, {2,0},  // bottom face
                {4,5}, {5,7}, {7,6}, {6,4},  // top face
                {0,4}, {1,5}, {3,7}, {2,6},  // vertical edges
            };
            for (int e = 0; e < 12; ++e) {
                Point3D c0 = corner(bar, edges[e][0]);
                Point3D c1 = corner(bar, edges[e][1]);
                edgePoints_.push_back(project(vp, c0.x, c0.y, c0.z));
                edgePoints_.push_back(project(vp, c1.x, c1.y, c1.z));
            }
        }
    }
}

void Bar3D::prepare(render::Renderer& r) {
    projectBars();

    auto& ctx = r.backend().context();

    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }

    if (config_.drawEdges && !edgePoints_.empty()) {
        lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{edgePoints_}, config_.edgeColor,
                             config_.edgeWidth);
    }

    prepared_ = true;
}

void Bar3D::draw(vk::CommandBuffer cmd, render::Renderer&,
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

    if (config_.drawEdges && edgePoints_.size() >= 2)
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(edgePoints_.size()));
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
