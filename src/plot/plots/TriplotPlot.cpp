// volcano/plot/plots/TriplotPlot.cpp — triangulation edge plot
#include "volcano/plot/plots/TriplotPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <stdexcept>

namespace volcano::plot {

TriplotPlot::TriplotPlot(std::vector<float> x, std::vector<float> y,
                         TriplotConfig config)
    : x_(std::move(x)), y_(std::move(y)), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("TriplotPlot: x and y must have the same size");
    if (x_.size() < 3)
        throw std::invalid_argument("TriplotPlot: need at least 3 points");
}

TriplotPlot::TriplotPlot(std::vector<float> x, std::vector<float> y,
                         std::vector<Triangle> triangles,
                         TriplotConfig config)
    : x_(std::move(x)), y_(std::move(y)), tris_(std::move(triangles)),
      config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("TriplotPlot: x and y must have the same size");
}

void TriplotPlot::buildEdges() {
    segments_.clear();
    // Each triangle has 3 edges. We emit all edges (duplicates between
    // adjacent triangles are fine for line rendering — they just overlap).
    for (const auto& tri : tris_) {
        Point2D a{x_[tri.a], y_[tri.a]};
        Point2D b{x_[tri.b], y_[tri.b]};
        Point2D c{x_[tri.c], y_[tri.c]};
        segments_.push_back(a); segments_.push_back(b);
        segments_.push_back(b); segments_.push_back(c);
        segments_.push_back(c); segments_.push_back(a);
    }

    if (config_.showMarkers) {
        points_.clear();
        pointColors_.clear();
        for (size_t i = 0; i < x_.size(); ++i) {
            points_.push_back({x_[i], y_[i]});
            pointColors_.push_back(config_.markerColor);
        }
    }
}

void TriplotPlot::prepare(render::Renderer& r) {
    // Triangulate if not provided.
    if (tris_.empty()) {
        std::vector<Point2D> pts(x_.size());
        for (size_t i = 0; i < x_.size(); ++i)
            pts[i] = {x_[i], y_[i]};
        tris_ = delaunay(pts);
    }

    buildEdges();

    auto& ctx = r.backend().context();
    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!segments_.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{segments_}, config_.color,
                             config_.lineWidth);
    }

    if (config_.showMarkers && !points_.empty()) {
        pointRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                            r.backend().sampleCount(), r.descriptorPool(),
                            r.pipelineCache());
        std::vector<float> sizes(points_.size(), config_.markerSize);
        pointRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{points_}, std::span{pointColors_},
                              std::span{sizes});
    }

    prepared_ = true;
}

void TriplotPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!segments_.empty())
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));

    if (config_.showMarkers && !points_.empty())
        pointRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(points_.size()));
}

void TriplotPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
