// volcano/plot/plots/TrisurfPlot.cpp — 3D triangulated surface implementation
#include "volcano/plot/plots/TrisurfPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

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

TrisurfPlot::TrisurfPlot(std::vector<float> x, std::vector<float> y,
                         std::vector<float> z, TrisurfConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TrisurfPlot: x, y, z must have the same size");
    // Delaunay triangulate (x, y) points.
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i)
        pts[i] = {x_[i], y_[i]};
    triangles_ = delaunay(pts);
}

TrisurfPlot::TrisurfPlot(std::vector<float> x, std::vector<float> y,
                         std::vector<float> z, std::vector<Triangle> triangles,
                         TrisurfConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      triangles_(std::move(triangles)), config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TrisurfPlot: x, y, z must have the same size");
}

Color TrisurfPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void TrisurfPlot::projectSurface() {
    fillPositions_.clear();
    fillColors_.clear();
    edgeSegments_.clear();

    if (x_.empty() || triangles_.empty()) return;

    auto vp = camera_.viewProjection();

    // Compute z range for color mapping.
    Range zRange = config_.valueRange;
    if (!zRange.valid()) {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float v : z_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        if (vmin > vmax) { vmin = 0; vmax = 1; }
        zRange = {vmin, vmax};
    }
    float zSpan = zRange.span();
    if (zSpan <= 0) zSpan = 1;

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();

    // Project each triangle and compute average depth for sorting.
    struct ProjectedTri {
        Point2D v0, v1, v2;
        float depth;
        Color color;
    };
    std::vector<ProjectedTri> tris;

    for (const auto& tri : triangles_) {
        if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
            continue;

        Point3D p0{x_[tri.a], y_[tri.a], z_[tri.a]};
        Point3D p1{x_[tri.b], y_[tri.b], z_[tri.b]};
        Point3D p2{x_[tri.c], y_[tri.c], z_[tri.c]};

        float avgDepth = (projectDepth(vp, p0.x, p0.y, p0.z) +
                          projectDepth(vp, p1.x, p1.y, p1.z) +
                          projectDepth(vp, p2.x, p2.y, p2.z)) / 3.0f;

        float avgZ = (p0.z + p1.z + p2.z) / 3.0f;
        float t = std::clamp((avgZ - zRange.min) / zSpan, 0.0f, 1.0f);
        Color color = cmap.sample(t);

        tris.push_back({
            project(vp, p0.x, p0.y, p0.z),
            project(vp, p1.x, p1.y, p1.z),
            project(vp, p2.x, p2.y, p2.z),
            avgDepth,
            color
        });
    }

    // Sort back-to-front (painter's algorithm).
    std::sort(tris.begin(), tris.end(),
              [](const ProjectedTri& a, const ProjectedTri& b) {
                  return a.depth > b.depth;
              });

    // Build fill triangles.
    for (const auto& tri : tris) {
        fillPositions_.push_back(tri.v0);
        fillPositions_.push_back(tri.v1);
        fillPositions_.push_back(tri.v2);
        for (int k = 0; k < 3; ++k) fillColors_.push_back(tri.color);
    }

    // Build edge segments.
    if (config_.drawEdges) {
        for (const auto& tri : triangles_) {
            if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
                continue;
            Point3D p0{x_[tri.a], y_[tri.a], z_[tri.a]};
            Point3D p1{x_[tri.b], y_[tri.b], z_[tri.b]};
            Point3D p2{x_[tri.c], y_[tri.c], z_[tri.c]};
            edgeSegments_.push_back(project(vp, p0.x, p0.y, p0.z));
            edgeSegments_.push_back(project(vp, p1.x, p1.y, p1.z));
            edgeSegments_.push_back(project(vp, p1.x, p1.y, p1.z));
            edgeSegments_.push_back(project(vp, p2.x, p2.y, p2.z));
            edgeSegments_.push_back(project(vp, p2.x, p2.y, p2.z));
            edgeSegments_.push_back(project(vp, p0.x, p0.y, p0.z));
        }
    }
}

void TrisurfPlot::prepare(render::Renderer& r) {
    projectSurface();

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

void TrisurfPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void TrisurfPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
        v.z.min = std::min(v.z.min, z_[i]);
        v.z.max = std::max(v.z.max, z_[i]);
    }
}

} // namespace volcano::plot
