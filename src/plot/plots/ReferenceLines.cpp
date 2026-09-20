// volcano/plot/plots/ReferenceLines.cpp
#include "volcano/plot/plots/ReferenceLines.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "../VectorEmitHelpers.hpp"
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
    // Convert data y to pixel y (Y-up data → Y-down pixel).
    float py = rect.y + (1.0f - axes.dataToFraction({0.0f, y_}).y) * rect.height;
    // Line spans the axes rect in pixel x. Extend slightly beyond to
    // ensure full pixel coverage at edges.
    Point2D pts[] = {
        {static_cast<float>(rect.x) - 10.0f, py},
        {static_cast<float>(rect.x + rect.width) + 10.0f, py}
    };
    // Clip to the axes patch (matplotlib clips these lines to the axes).
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    r.spineRenderer().drawLineStrip(cmd, clip, r.backend().extent(),
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
    // Convert data x to pixel x.
    float px = rect.x + axes.dataToFraction({x_, 0.0f}).x * rect.width;
    // Line spans the axes rect in pixel y. Extend slightly beyond to
    // ensure full pixel coverage at edges.
    Point2D pts[] = {
        {px, static_cast<float>(rect.y) - 10.0f},
        {px, static_cast<float>(rect.y + rect.height) + 10.0f}
    };
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    r.spineRenderer().drawLineStrip(cmd, clip, r.backend().extent(),
                                    std::span{pts, 2}, color_, width_);
}

void AxvLine::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, x_);
    v.x.max = std::max(v.x.max, x_);
}

// ═══════════════════════════════════════════════════════════════════════════
// AxLine — infinite line through two points (mpl axline)
// ═══════════════════════════════════════════════════════════════════════════

void AxLine::prepare(render::Renderer& /*r*/) {
    prepared_ = true;
}

void AxLine::draw(vk::CommandBuffer cmd, render::Renderer& r,
                  const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    // mpl axline clips to the axes patch; the line is infinite in data
    // space. Working in pixel space keeps the math degenerate-free
    // (vertical lines, huge slopes) — compute the two pixel points, then
    // extend the direction far past the rect edges and let the scissor
    // clip it.
    auto toPx = [&](Point2D d) {
        auto f = axes.dataToFraction(d);
        return Point2D{rect.x + f.x * rect.width,
                       rect.y + (1.0f - f.y) * rect.height};
    };
    Point2D p1 = toPx(xy1_), p2 = toPx(xy2_);
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return;  // coincident points — no line defined
    dx /= len; dy /= len;
    float ext = float(rect.width + rect.height) * 2.0f + 40.0f;
    Point2D pts[] = {
        {p1.x - dx * ext, p1.y - dy * ext},
        {p1.x + dx * ext, p1.y + dy * ext}
    };
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    r.spineRenderer().drawLineStrip(cmd, clip, r.backend().extent(),
                                    std::span{pts, 2}, color_, width_);
}

void AxLine::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, std::min(xy1_.x, xy2_.x));
    v.x.max = std::max(v.x.max, std::max(xy1_.x, xy2_.x));
    v.y.min = std::min(v.y.min, std::min(xy1_.y, xy2_.y));
    v.y.max = std::max(v.y.max, std::max(xy1_.y, xy2_.y));
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
    Transform2D t = axes.transform();
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
    Transform2D t = axes.transform();
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
    Transform2D t = axes.transform();
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
    Transform2D t = axes.transform();
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

// ═══════════════════════════════════════════════════════════════════════════
// Vector emit for the reference lines/spans
// ═══════════════════════════════════════════════════════════════════════════

void AxhLine::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    float py = rect.y +
        (1.0f - axes.dataToFraction({0.0f, y_}).y) * float(rect.height);
    render::VectorCanvas::Pen pen;
    pen.color = color_; pen.width = width_;
    Point2D seg[2] = {{float(rect.x), py}, {float(rect.x + rect.width), py}};
    c.polyline(seg, pen);
}

void AxvLine::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    float px = rect.x +
        axes.dataToFraction({x_, 0.0f}).x * float(rect.width);
    render::VectorCanvas::Pen pen;
    pen.color = color_; pen.width = width_;
    Point2D seg[2] = {{px, float(rect.y)}, {px, float(rect.y + rect.height)}};
    c.polyline(seg, pen);
}

void AxLine::emitVector(render::VectorCanvas& c, const Axes& axes,
                        Rect2D rect) {
    auto toPx = [&](Point2D d) {
        auto f = axes.dataToFraction(d);
        return Point2D{rect.x + f.x * rect.width,
                       rect.y + (1.0f - f.y) * rect.height};
    };
    Point2D p1 = toPx(xy1_), p2 = toPx(xy2_);
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    // Liang–Barsky clip of the segment p1->p2 against the axes rect,
    // widened so the infinite line is bounded by the rect edges.
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = float(rect.x + rect.width), y1 = float(rect.y + rect.height);
    float t0 = -1e9f, t1 = 1e9f;
    auto clip1 = [&](float p, float q) -> int {
        if (p == 0.0f) return q < 0.0f ? 0 : 1;
        float tt = q / p;
        if (p < 0.0f) { if (tt > t1) return 0; if (tt > t0) t0 = tt; }
        else          { if (tt < t0) return 0; if (tt < t1) t1 = tt; }
        return 1;
    };
    if (!clip1(-dx, p1.x - x0) || !clip1(dx, x1 - p1.x) ||
        !clip1(-dy, p1.y - y0) || !clip1(dy, y1 - p1.y))
        return;
    Point2D seg[2] = {{p1.x + t0 * dx, p1.y + t0 * dy},
                      {p1.x + t1 * dx, p1.y + t1 * dy}};
    render::VectorCanvas::Pen pen;
    pen.color = color_; pen.width = width_;
    c.polyline(seg, pen);
}

void AxhSpan::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    float pa = rect.y +
        (1.0f - axes.dataToFraction({0.0f, std::min(y1_, y2_)}).y) *
            float(rect.height);
    float pb = rect.y +
        (1.0f - axes.dataToFraction({0.0f, std::max(y1_, y2_)}).y) *
            float(rect.height);
    Point2D q[4] = {{float(rect.x), pb}, {float(rect.x + rect.width), pb},
                    {float(rect.x + rect.width), pa}, {float(rect.x), pa}};
    c.polygon(q, color_);
}

void AxvSpan::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    float pa = rect.x +
        axes.dataToFraction({std::min(x1_, x2_), 0.0f}).x *
            float(rect.width);
    float pb = rect.x +
        axes.dataToFraction({std::max(x1_, x2_), 0.0f}).x *
            float(rect.width);
    Point2D q[4] = {{pa, float(rect.y)}, {pb, float(rect.y)},
                    {pb, float(rect.y + rect.height)},
                    {pa, float(rect.y + rect.height)}};
    c.polygon(q, color_);
}

void Vlines::emitVector(render::VectorCanvas& c, const Axes& axes,
                        Rect2D rect) {
    auto toPx = pxMapper(axes, rect);
    render::VectorCanvas::Pen pen;
    pen.color = color_; pen.width = width_;
    for (float x : xPositions_) {
        Point2D seg[2] = {toPx({x, yMin_}), toPx({x, yMax_})};
        c.polyline(seg, pen);
    }
}

void Hlines::emitVector(render::VectorCanvas& c, const Axes& axes,
                        Rect2D rect) {
    auto toPx = pxMapper(axes, rect);
    render::VectorCanvas::Pen pen;
    pen.color = color_; pen.width = width_;
    for (float y : yPositions_) {
        Point2D seg[2] = {toPx({xMin_, y}), toPx({xMax_, y})};
        c.polyline(seg, pen);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// EventPlot
// ═══════════════════════════════════════════════════════════════════════════

void EventPlot::buildRows() {
    rowSegs_.assign(positions_.size(), {});
    rowColors_.assign(positions_.size(), color);
    rowWidths_.assign(positions_.size(), lineWidth);
    bool vert = orientation == "vertical";
    for (size_t i = 0; i < positions_.size(); ++i) {
        float off = i < lineoffsets.size() ? lineoffsets[i] : float(i);
        float half = (i < linelengths.size() ? linelengths[i] : 1.0f) * 0.5f;
        if (i < colors.size()) rowColors_[i] = colors[i];
        if (i < linewidths.size()) rowWidths_[i] = linewidths[i];
        auto& segs = rowSegs_[i];
        for (float p : positions_[i]) {
            if (vert) {
                segs.push_back({off - half, p});
                segs.push_back({off + half, p});
            } else {
                segs.push_back({p, off - half});
                segs.push_back({p, off + half});
            }
        }
    }
}

void EventPlot::prepare(render::Renderer& r) {
    buildRows();
    auto& ctx = r.backend().context();
    renderers_.clear();
    for (size_t i = 0; i < rowSegs_.size(); ++i) {
        if (rowSegs_[i].empty()) continue;
        auto sr = std::make_unique<render::primitives::LineSegmentRenderer>();
        sr->init(ctx.device.handle(), r.backend().renderPass(),
                 r.backend().sampleCount(), r.pipelineCache());
        sr->upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                   ctx.graphicsPool.handle(), ctx.allocator.handle(),
                   std::span{rowSegs_[i]}, rowColors_[i], rowWidths_[i]);
        renderers_.push_back(std::move(sr));
    }
    prepared_ = true;
}

void EventPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    size_t row = 0;
    for (auto& sr : renderers_) {
        while (row < rowSegs_.size() && rowSegs_[row].empty()) ++row;
        if (row >= rowSegs_.size()) break;
        sr->draw(cmd, vrect, t, uint32_t(rowSegs_[row++].size()));
    }
}

void EventPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                           Rect2D rect) {
    if (rowSegs_.empty()) buildRows();
    auto toPx = pxMapper(axes, rect);
    for (size_t i = 0; i < rowSegs_.size(); ++i) {
        render::VectorCanvas::Pen pen;
        pen.color = rowColors_[i];
        pen.width = rowWidths_[i];
        for (size_t k = 0; k + 1 < rowSegs_[i].size(); k += 2) {
            Point2D seg[2] = {toPx(rowSegs_[i][k]), toPx(rowSegs_[i][k + 1])};
            c.polyline(seg, pen);
        }
    }
}

void EventPlot::contributeToAutoscale(Viewport& v) const {
    bool vert = orientation == "vertical";
    for (size_t i = 0; i < positions_.size(); ++i) {
        float off = i < lineoffsets.size() ? lineoffsets[i] : float(i);
        float half = (i < linelengths.size() ? linelengths[i] : 1.0f) * 0.5f;
        for (float p : positions_[i]) {
            if (vert) {
                v.x.min = std::min(v.x.min, off - half);
                v.x.max = std::max(v.x.max, off + half);
                v.y.min = std::min(v.y.min, p);
                v.y.max = std::max(v.y.max, p);
            } else {
                v.x.min = std::min(v.x.min, p);
                v.x.max = std::max(v.x.max, p);
                v.y.min = std::min(v.y.min, off - half);
                v.y.max = std::max(v.y.max, off + half);
            }
        }
    }
}

} // namespace volcano::plot
