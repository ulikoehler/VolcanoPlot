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
#include <cmath>
namespace volcano::plot {
void LinePlot::setData(std::vector<float> x, std::vector<float> y) {
    const size_t n = std::min(x.size(), y.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i)
        series_.points[i] = {x[i], y[i]};
    dataDirty_ = true;
    touch();
}
void LinePlot::setXdata(std::vector<float> x) {
    const size_t n = std::min(x.size(), series_.points.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i) series_.points[i].x = x[i];
    dataDirty_ = true;
    touch();
}
void LinePlot::setYdata(std::vector<float> y) {
    const size_t n = std::min(y.size(), series_.points.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i) series_.points[i].y = y[i];
    dataDirty_ = true;
    touch();
}

void LinePlot::prepare(render::Renderer& r) {
    if (prepared_) {
        // In-place update: memcpy into the host-visible buffer (reallocs
        // only on growth). Direct series() writes stay correct — there is
        // no dirty flag to bypass, the upload is just cheap.
        renderer_.updatePoints(std::span{series_.points});
        dataDirty_ = false;
        return;
    }
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, series_.resolvedColor(), series_.lineWidth);
    prepared_ = true;
    dataDirty_ = false;
}
namespace {

/// Shared pixel-space polyline computation: drawStyle expansion, scale
/// masking (NaN splits) and data→pixel transform. Used by both the GPU
/// pre-pass (preDraw) and the CPU fallback in draw().
std::vector<Point2D> pixelPoints(const Series2D& series, const Axes& axes,
                                 Rect2D rect) {
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series.points;
    if (axes.xscale().clipsDomain() || axes.yscale().clipsDomain()) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }
    auto pts = applyDrawStyle(src, series.drawStyle);
    std::vector<Point2D> px;
    px.reserve(pts.size());
    for (const auto& p : pts) {
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    return px;
}

} // namespace

void LinePlot::preDraw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_ || series_.points.size() < 2 ||
        series_.lineStyle == LineStyle::None ||
        axes.style().sketchScale > 0.0f)
        return;
    // Dashed/sketch lines stay on the CPU stroker — the GPU path only
    // handles solid strokes.
    StrokeParams sp;
    sp.width = series_.lineWidth;
    sp.dashes = series_.dashes.empty()
                    ? dashPattern(series_.lineStyle, series_.lineWidth)
                    : series_.dashes;
    if (!sp.dashes.empty()) return;
    sp.join = series_.joinStyle;
    sp.cap = series_.capStyle;
    auto& gpu = r.gpuLineRenderer();
    if (!gpu.inited()) return;
    auto px = pixelPoints(series_, axes, rect);
    gpuMesh_ = gpu.tessellate(cmd, px, sp, series_.resolvedColor());
    gpuMeshSeq_ = r.frameSeq();
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

    auto px = pixelPoints(series_, axes, rect);
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

    // Solid lines were tessellated on the GPU in preDraw; dashes fall
    // back to the CPU stroker.
    if (gpuMeshSeq_ == r.frameSeq() && gpuMesh_.buffer) {
        spine.drawTrianglesGpu(cmd, clip, res, gpuMesh_.buffer,
                               gpuMesh_.firstVertex * 6 * sizeof(float),
                               gpuMesh_.vertexCount);
    } else {
        auto mesh = strokePolyline(px, sp);
        spine.drawTriangles(cmd, clip, res, mesh.verts,
                            series_.resolvedColor());
    }

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
        // Drop out-of-domain points (log/logit scales mask them).
        if (!pointInDomain(p, axes.xscale(), axes.yscale()) ||
            !std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    if (series_.markerPath) {
        drawMarkersPx(r, cmd, clip, px, markerGeom(*series_.markerPath),
                      series_.size, series_.resolvedColor(),
                      std::max(1.0f, series_.size * 0.1f));
    } else if (!series_.markerTex.empty()) {
        drawTexMarkersPx(r, cmd, clip, px, series_.markerTex,
                         series_.resolvedColor(), series_.size);
    } else if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        drawMarkersPx(r, cmd, clip, px, g, series_.size, series_.resolvedColor(),
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
    // Mask out-of-domain data (log/logit) — NaN points split polylines
    // and are skipped by emitMarkerAt.
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series_.points;
    if (axes.xscale().clipsDomain() || axes.yscale().clipsDomain()) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }
    // Line.
    if (series_.lineStyle != LineStyle::None && series_.points.size() >= 2) {
        auto pts = applyDrawStyle(src, series_.drawStyle);
        std::vector<Point2D> px;
        px.reserve(pts.size());
        for (const auto& p : pts) px.push_back(toPx(p));
        if (axes.style().sketchScale > 0.0f)
            px = sketchPolyline(px, axes.style().sketchScale * 2.0f);
        render::VectorCanvas::Pen pen;
        pen.color = series_.resolvedColor();
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
        for (const auto& dp : src) {
            auto p = toPx(dp);
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
            c.text({p.x - halfW, p.y + series_.size * 0.35f},
                   uni, series_.size, series_.resolvedColor());
        }
        return;
    }
    if (series_.markerPath) {
        emitMarkerAt(c, toPx, src,
                     markerGeom(*series_.markerPath), series_.size,
                     series_.resolvedColor(), std::max(1.0f, series_.size * 0.1f));
        return;
    }
    if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        emitMarkerAt(c, toPx, src, g, series_.size,
                     series_.resolvedColor(), 1.0f);
    }
}
void LinePlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}
void LinePlot::contributeToAutoscaleScaled(Viewport& v,
                                           const AxisScale& xscale,
                                           const AxisScale& yscale) const {
    for (const auto& p : series_.points) {
        if (!pointInDomain(p, xscale, yscale) ||
            !std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
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
