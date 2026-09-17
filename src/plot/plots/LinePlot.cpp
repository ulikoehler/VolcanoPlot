// volcano/plot/plots/LinePlot.cpp
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/text/MathText.hpp"
#include "../MarkerDraw.hpp"
#include "../VectorEmitHelpers.hpp"
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
    if (!prepared_ || series_.points.empty()) return;
    if (series_.points.size() < 2 ||
        series_.lineStyle == LineStyle::None) {
        // Marker-only (linestyle 'None') still draws markers.
        drawMarkersAtPoints(cmd, r, axes, rect);
        return;
    }

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

    // Markers at each vertex (matplotlib plot marker=...).
    drawMarkersAtPoints(cmd, r, axes, rect);
}

void LinePlot::drawMarkersAtPoints(vk::CommandBuffer cmd,
                                   render::Renderer& r,
                                   const Axes& axes, Rect2D rect) {
    if (series_.size <= 0) return;
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    std::vector<Point2D> px;
    px.reserve(series_.points.size());
    for (const auto& p : series_.points) {
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    if (series_.markerPath) {
        drawMarkersPx(r, cmd, clip, px, markerGeom(*series_.markerPath),
                      series_.size, series_.color,
                      std::max(1.0f, series_.size * 0.1f));
    } else if (!series_.markerTex.empty()) {
        drawTexMarkersPx(r, cmd, clip, px, series_.markerTex,
                         series_.color, series_.size);
    } else if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        drawMarkersPx(r, cmd, clip, px, g, series_.size, series_.color,
                      std::max(1.0f, series_.size * 0.1f));
    }
}
void LinePlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (series_.points.empty()) return;
    auto toPx = [&](Point2D p) {
        auto f = axes.dataToFraction(p);
        return Point2D{rect.x + f.x * float(rect.width),
                       rect.y + (1.0f - f.y) * float(rect.height)};
    };
    // Line.
    if (series_.lineStyle != LineStyle::None && series_.points.size() >= 2) {
        auto pts = applyDrawStyle(series_.points, series_.drawStyle);
        std::vector<Point2D> px;
        px.reserve(pts.size());
        for (const auto& p : pts) px.push_back(toPx(p));
        if (axes.style().sketchScale > 0.0f)
            px = sketchPolyline(px, axes.style().sketchScale * 2.0f);
        render::VectorCanvas::Pen pen;
        pen.color = series_.color;
        pen.width = series_.lineWidth;
        pen.dashes = series_.dashes.empty()
            ? dashPattern(series_.lineStyle, series_.lineWidth)
            : series_.dashes;
        pen.dashOffset = series_.dashOffset;
        pen.join = series_.joinStyle;
        pen.cap = series_.capStyle;
        // gapcolor underlay.
        if (series_.gapColor.a > 0.0f && !pen.dashes.empty()) {
            auto solid = pen; solid.color = series_.gapColor;
            solid.dashes.clear();
            c.polyline(px, solid);
        }
        c.polyline(px, pen);
    }
    // Markers at each point.
    if (series_.size <= 0) return;
    if (!series_.markerTex.empty()) {
        auto uni = text::mathTextToUnicode(series_.markerTex);
        const float halfW = series_.size * 0.3f * float(uni.size());
        for (const auto& dp : series_.points) {
            auto p = toPx(dp);
            c.text({p.x - halfW, p.y + series_.size * 0.35f},
                   uni, series_.size, series_.color);
        }
        return;
    }
    if (series_.markerPath) {
        emitMarkerAt(c, toPx, series_.points,
                     markerGeom(*series_.markerPath), series_.size,
                     series_.color, std::max(1.0f, series_.size * 0.1f));
        return;
    }
    if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        emitMarkerAt(c, toPx, series_.points, g, series_.size,
                     series_.color, 1.0f);
    }
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
