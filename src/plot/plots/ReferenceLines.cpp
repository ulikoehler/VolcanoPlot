// volcano/plot/plots/ReferenceLines.cpp
#include "volcano/plot/plots/ReferenceLines.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>

namespace volcano::plot {

// ═══════════════════════════════════════════════════════════════════════════
// AxhLine — drawn via SpineRenderer's pixel-space line strip.
// Converts data y to pixel y, then draws a horizontal line spanning
// the axes rect in pixel coordinates. This avoids GPU guard-band issues
// with line primitives in data space.
// ═══════════════════════════════════════════════════════════════════════════

void AxhLine::prepare(render::Renderer& /*r*/) {
    prepared_ = true;
}

void AxhLine::draw(vk::CommandBuffer cmd, render::Renderer& r,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    const auto& vp = axes.viewport();
    // Convert data y to pixel y (Y-up data → Y-down pixel).
    float py = rect.y + (1.0f - (y_ - vp.y.min) / vp.y.span()) * rect.height;
    // Line spans the axes rect in pixel x. Extend slightly beyond to
    // ensure full pixel coverage at edges.
    Point2D pts[] = {
        {static_cast<float>(rect.x) - 10.0f, py},
        {static_cast<float>(rect.x + rect.width) + 10.0f, py}
    };
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    r.spineRenderer().drawLineStrip(cmd, fullRect,
                                    std::span{pts, 2}, color_, width_);
}

void AxhLine::contributeToAutoscale(Viewport& v) const {
    v.y.min = std::min(v.y.min, y_);
    v.y.max = std::max(v.y.max, y_);
}

// ═══════════════════════════════════════════════════════════════════════════
// AxvLine
// ═══════════════════════════════════════════════════════════════════════════

void AxvLine::prepare(render::Renderer& /*r*/) {
    prepared_ = true;
}

void AxvLine::draw(vk::CommandBuffer cmd, render::Renderer& r,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    const auto& vp = axes.viewport();
    // Convert data x to pixel x.
    float px = rect.x + (x_ - vp.x.min) / vp.x.span() * rect.width;
    // Line spans the axes rect in pixel y. Extend slightly beyond to
    // ensure full pixel coverage at edges.
    Point2D pts[] = {
        {px, static_cast<float>(rect.y) - 10.0f},
        {px, static_cast<float>(rect.y + rect.height) + 10.0f}
    };
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    r.spineRenderer().drawLineStrip(cmd, fullRect,
                                    std::span{pts, 2}, color_, width_);
}

void AxvLine::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, x_);
    v.x.max = std::max(v.x.max, x_);
}

// ═══════════════════════════════════════════════════════════════════════════
// AxhSpan
// ═══════════════════════════════════════════════════════════════════════════

void AxhSpan::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    // Filled rectangle from (-inf, y1) to (+inf, y2).
    float lo = std::min(y1_, y2_);
    float hi = std::max(y1_, y2_);
    Point2D verts[] = {
        {-kAxisSpan, lo}, {kAxisSpan, lo}, {-kAxisSpan, hi},
        {kAxisSpan, lo}, {kAxisSpan, hi}, {-kAxisSpan, hi}
    };
    Color colors[6] = {color_, color_, color_, color_, color_, color_};
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{verts, 6}, std::span{colors, 6});
    prepared_ = true;
}

void AxhSpan::draw(vk::CommandBuffer cmd, render::Renderer&,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void AxhSpan::contributeToAutoscale(Viewport& v) const {
    v.y.min = std::min(v.y.min, std::min(y1_, y2_));
    v.y.max = std::max(v.y.max, std::max(y1_, y2_));
}

// ═══════════════════════════════════════════════════════════════════════════
// AxvSpan
// ═══════════════════════════════════════════════════════════════════════════

void AxvSpan::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    // Filled rectangle from (x1, -inf) to (x2, +inf).
    float lo = std::min(x1_, x2_);
    float hi = std::max(x1_, x2_);
    Point2D verts[] = {
        {lo, -kAxisSpan}, {hi, -kAxisSpan}, {lo, kAxisSpan},
        {hi, -kAxisSpan}, {hi, kAxisSpan}, {lo, kAxisSpan}
    };
    Color colors[6] = {color_, color_, color_, color_, color_, color_};
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{verts, 6}, std::span{colors, 6});
    prepared_ = true;
}

void AxvSpan::draw(vk::CommandBuffer cmd, render::Renderer&,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void AxvSpan::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, std::min(x1_, x2_));
    v.x.max = std::max(v.x.max, std::max(x1_, x2_));
}

// ═══════════════════════════════════════════════════════════════════════════
// Vlines
// ═══════════════════════════════════════════════════════════════════════════

void Vlines::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    // Build line segments: (x[i], yMin) → (x[i], yMax) for each x.
    std::vector<Point2D> segs;
    segs.reserve(xPositions_.size() * 2);
    for (float x : xPositions_) {
        segs.push_back({x, yMin_});
        segs.push_back({x, yMax_});
    }
    vertexCount_ = static_cast<uint32_t>(segs.size());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{segs.data(), segs.size()}, color_, width_);
    prepared_ = true;
}

void Vlines::draw(vk::CommandBuffer cmd, render::Renderer&,
                  const Axes& axes, Rect2D rect) {
    if (!prepared_ || vertexCount_ < 2) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, vertexCount_);
}

void Vlines::contributeToAutoscale(Viewport& v) const {
    for (float x : xPositions_) {
        v.x.min = std::min(v.x.min, x);
        v.x.max = std::max(v.x.max, x);
    }
    v.y.min = std::min(v.y.min, yMin_);
    v.y.max = std::max(v.y.max, yMax_);
}

// ═══════════════════════════════════════════════════════════════════════════
// Hlines
// ═══════════════════════════════════════════════════════════════════════════

void Hlines::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    // Build line segments: (xMin, y[i]) → (xMax, y[i]) for each y.
    std::vector<Point2D> segs;
    segs.reserve(yPositions_.size() * 2);
    for (float y : yPositions_) {
        segs.push_back({xMin_, y});
        segs.push_back({xMax_, y});
    }
    vertexCount_ = static_cast<uint32_t>(segs.size());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{segs.data(), segs.size()}, color_, width_);
    prepared_ = true;
}

void Hlines::draw(vk::CommandBuffer cmd, render::Renderer&,
                  const Axes& axes, Rect2D rect) {
    if (!prepared_ || vertexCount_ < 2) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, vertexCount_);
}

void Hlines::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, xMin_);
    v.x.max = std::max(v.x.max, xMax_);
    for (float y : yPositions_) {
        v.y.min = std::min(v.y.min, y);
        v.y.max = std::max(v.y.max, y);
    }
}

} // namespace volcano::plot
