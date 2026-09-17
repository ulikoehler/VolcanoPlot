// volcano/plot/plots/LinePlot.cpp
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include "volcano/plot/Stroke.hpp"
#include <algorithm>
namespace volcano::plot {
void LinePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, series_.color, series_.lineWidth);
    prepared_ = true;
}
void LinePlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || series_.points.size() < 2 ||
        series_.lineStyle == LineStyle::None) return;

    // Expand the point sequence for step draw styles, then map data → pixels.
    auto pts = applyDrawStyle(series_.points, series_.drawStyle);
    std::vector<Point2D> px;
    px.reserve(pts.size());
    for (const auto& p : pts) {
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    // xkcd-style sketch wobble (path.sketch).
    if (axes.style().sketchScale > 0.0f)
        px = sketchPolyline(px, axes.style().sketchScale * 2.0f);

    StrokeParams sp;
    sp.width = series_.lineWidth;
    sp.dashes = series_.dashes.empty()
                    ? dashPattern(series_.lineStyle, series_.lineWidth)
                    : series_.dashes;
    sp.dashOffset = series_.dashOffset;
    sp.join = series_.joinStyle;
    sp.cap = series_.capStyle;

    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    vk::Extent2D res = r.backend().extent();
    auto& spine = r.spineRenderer();

    // gapcolor: solid underlay first, dashed line on top.
    if (series_.gapColor.a > 0.0f && !sp.dashes.empty()) {
        StrokeParams solid = sp;
        solid.dashes.clear();
        auto under = strokePolyline(px, solid);
        spine.drawTriangles(cmd, clip, res, under.verts, series_.gapColor);
    }

    auto mesh = strokePolyline(px, sp);
    spine.drawTriangles(cmd, clip, res, mesh.verts, series_.color);
}
void LinePlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}
void LinePlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(), renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX); v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY); v.y.max = std::max(v.y.max, r->maxY);
}
} // namespace volcano::plot
