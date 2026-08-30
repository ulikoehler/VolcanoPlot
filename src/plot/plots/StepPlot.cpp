// volcano/plot/plots/StepPlot.cpp — step and stairs plot implementation
#include "volcano/plot/plots/StepPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <stdexcept>

namespace volcano::plot {

// ═══════════════════════════════════════════════════════════════════════════
// StepPlot
// ═══════════════════════════════════════════════════════════════════════════

StepPlot::StepPlot(std::vector<float> x, std::vector<float> y,
                   StepWhere where, Color color, float lineWidth)
    : x_(std::move(x)), y_(std::move(y)), where_(where),
      color_(color), lineWidth_(lineWidth) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("StepPlot: x and y must have the same size");
}

void StepPlot::buildStepPoints() {
    stepPoints_.clear();
    size_t n = x_.size();
    if (n == 0) return;
    if (n == 1) {
        stepPoints_.push_back({x_[0], y_[0]});
        return;
    }
    switch (where_) {
    case StepWhere::Pre:
        // Horizontal then vertical: at each x[i], y is y[i-1] then jumps to y[i].
        stepPoints_.push_back({x_[0], y_[0]});
        for (size_t i = 1; i < n; ++i) {
            stepPoints_.push_back({x_[i], y_[i - 1]});  // horizontal to x[i]
            stepPoints_.push_back({x_[i], y_[i]});       // vertical to y[i]
        }
        break;
    case StepWhere::Post:
        // Vertical then horizontal: at each x[i], y jumps to y[i] then goes horizontal.
        stepPoints_.push_back({x_[0], y_[0]});
        for (size_t i = 1; i < n; ++i) {
            stepPoints_.push_back({x_[i - 1], y_[i]});  // vertical to y[i]
            stepPoints_.push_back({x_[i], y_[i]});       // horizontal to x[i]
        }
        break;
    case StepWhere::Mid:
        // Step at midpoint between x[i-1] and x[i].
        stepPoints_.push_back({x_[0], y_[0]});
        for (size_t i = 1; i < n; ++i) {
            float mid = (x_[i - 1] + x_[i]) * 0.5f;
            stepPoints_.push_back({mid, y_[i - 1]});  // horizontal to mid
            stepPoints_.push_back({mid, y_[i]});       // vertical to y[i]
            stepPoints_.push_back({x_[i], y_[i]});     // horizontal to x[i]
        }
        break;
    }
}

void StepPlot::prepare(render::Renderer& r) {
    buildStepPoints();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!stepPoints_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{stepPoints_}, color_, lineWidth_);
    }
    prepared_ = true;
}

void StepPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || stepPoints_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(stepPoints_.size()));
}

void StepPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

void StepPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                    renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

// ═══════════════════════════════════════════════════════════════════════════
// StairsPlot
// ═══════════════════════════════════════════════════════════════════════════

StairsPlot::StairsPlot(std::vector<float> values, std::vector<float> edges,
                       Color color, float lineWidth, bool fill, Color fillColor)
    : values_(std::move(values)), edges_(std::move(edges)),
      color_(color), fillColor_(fillColor), lineWidth_(lineWidth), fill_(fill) {
    if (edges_.size() != values_.size() + 1)
        throw std::invalid_argument("StairsPlot: edges must have N+1 elements");
}

void StairsPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();

    // Build staircase outline: (edges[0], values[0]) → (edges[1], values[0])
    // → (edges[1], values[1]) → ... → (edges[N], values[N-1]).
    stepPoints_.clear();
    size_t n = values_.size();
    if (n > 0) {
        stepPoints_.push_back({edges_[0], values_[0]});
        for (size_t i = 1; i < n; ++i) {
            stepPoints_.push_back({edges_[i], values_[i - 1]});
            stepPoints_.push_back({edges_[i], values_[i]});
        }
        stepPoints_.push_back({edges_[n], values_[n - 1]});
    }

    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                      r.backend().sampleCount(), r.pipelineCache());
    if (!stepPoints_.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{stepPoints_}, color_, lineWidth_);
    }

    // Build fill triangles if requested.
    if (fill_ && n > 0) {
        fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                          r.backend().sampleCount(), r.pipelineCache());
        fillPositions_.clear();
        fillColors_.clear();
        for (size_t i = 0; i < n; ++i) {
            // Each step is a rectangle from (edges[i], 0) to (edges[i+1], values[i]).
            Point2D bl{edges_[i], 0.0f};
            Point2D br{edges_[i + 1], 0.0f};
            Point2D ul{edges_[i], values_[i]};
            Point2D ur{edges_[i + 1], values_[i]};
            // Triangle 1: bl, br, ul
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            // Triangle 2: br, ur, ul
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int j = 0; j < 6; ++j) fillColors_.push_back(fillColor_);
        }
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }

    prepared_ = true;
}

void StairsPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    if (fill_ && !fillPositions_.empty())
        fillRenderer_.draw(cmd, vrect, t);
    if (!stepPoints_.empty())
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(stepPoints_.size()));
}

void StairsPlot::contributeToAutoscale(Viewport& v) const {
    for (float e : edges_) {
        v.x.min = std::min(v.x.min, e);
        v.x.max = std::max(v.x.max, e);
    }
    for (float val : values_) {
        v.y.min = std::min(v.y.min, val);
        v.y.max = std::max(v.y.max, val);
    }
    // If filling, y=0 is also part of the data.
    if (fill_) {
        v.y.min = std::min(v.y.min, 0.0f);
        v.y.max = std::max(v.y.max, 0.0f);
    }
}

} // namespace volcano::plot
