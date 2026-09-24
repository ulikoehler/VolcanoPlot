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
#include <numeric>
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
/// pre-pass (preDraw) and the CPU fallback in draw(). A non-null
/// `xf` (mpl transform=) bypasses dataToFraction — it maps to display
/// pixels directly.
std::vector<Point2D> pixelPoints(const Series2D& series, const Axes& axes,
                                 Rect2D rect, const Transform* xf = nullptr) {
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series.points;
    if (!xf && (axes.xscale().clipsDomain() || axes.yscale().clipsDomain())) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }
    auto pts = applyDrawStyle(src, series.drawStyle);
    std::vector<Point2D> px;
    px.reserve(pts.size());
    for (const auto& p : pts) {
        if (xf) {
            px.push_back(xf->apply(p));
            continue;
        }
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    return px;
}

/// mpl `Line2D` markevery subsampling (lines._mark_every_path): returns
/// the indices into `px` — the finite display-space marker positions —
/// that get a marker. `diag` is the axes-box diagonal in px, which mpl
/// uses to scale the fractional form (the transAxes bbox diagonal).
std::vector<size_t> markerIndices(const Series2D& series,
                                  std::span<const Point2D> px,
                                  float diag) {
    const size_t n = px.size();
    if (n == 0) return {};
    if (!series.markevery) {
        std::vector<size_t> all(n);
        std::iota(all.begin(), all.end(), size_t{0});
        return all;
    }
    std::vector<size_t> out;
    // verts[slice(start, None, step)] — python slice semantics.
    auto countForm = [&](long start, long step) {
        if (start < 0) start += long(n);
        if (step <= 0) {
            if (start >= 0 && start < long(n)) out.push_back(size_t(start));
            return;
        }
        for (long i = start; i < long(n); i += step)
            out.push_back(size_t(i));
    };
    // (start, step) float form: targets along the cumulative arc length
    // at `step` × diag spacing; the nearest actual vertex is used.
    auto fracForm = [&](double start, double step) {
        if (step <= 0 || diag <= 0) return;
        std::vector<double> delta(n);
        for (size_t i = 1; i < n; ++i)
            delta[i] = delta[i - 1] +
                std::hypot(px[i].x - px[i - 1].x, px[i].y - px[i - 1].y);
        const double scale = double(diag);
        for (double d = start * scale; d < delta.back(); d += step * scale) {
            size_t best = 0;
            double bd = std::abs(delta[0] - d);
            for (size_t i = 1; i < n; ++i) {
                double dd = std::abs(delta[i] - d);
                if (dd < bd) { bd = dd; best = i; }
            }
            // argmin is non-decreasing in d → consecutive dedup = unique.
            if (out.empty() || out.back() != best) out.push_back(best);
        }
    };
    const MarkerEvery& me = *series.markevery;
    if (const auto* v = std::get_if<int>(&me)) countForm(0, *v);
    else if (const auto* p = std::get_if<std::pair<int, int>>(&me))
        countForm(p->first, p->second);
    else if (const auto* v = std::get_if<float>(&me)) fracForm(0.0, *v);
    else if (const auto* p = std::get_if<std::pair<float, float>>(&me))
        fracForm(p->first, p->second);
    else if (const auto* idx = std::get_if<std::vector<int>>(&me)) {
        for (int i : *idx) {
            long j = i < 0 ? long(n) + i : i;
            if (j >= 0 && j < long(n)) out.push_back(size_t(j));
        }
    }
    return out;
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
    auto px = pixelPoints(series_, axes, rect, transform.get());
    gpuMesh_ = gpu.tessellate(cmd, px, sp, series_.resolvedColor());
    gpuMeshSeq_ = r.frameSeq();
}

void LinePlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || series_.points.empty()) return;
    const float dpi = axes.style().dpi;
    if (series_.points.size() < 2 ||
        series_.lineStyle == LineStyle::None) {
        // Marker-only (linestyle 'None') still draws markers.
        if (pathEffects.empty()) {
            drawMarkersAtPoints(cmd, r, axes, rect);
            return;
        }
        for (const auto& fx : pathEffects) {
            MarkerFx mfx;
            Point2D off = fx.offsetPx(dpi);
            mfx.offset = off;
            switch (fx.kind) {
            case PathEffect::Kind::Stroke:
                mfx.edge = fx.foreground.value_or(series_.resolvedColor());
                mfx.edgeWidth = fx.strokeWidthPx(
                    std::max(series_.markerEdgeWidth, 0.5f), dpi);
                break;
            case PathEffect::Kind::LineShadow:
            case PathEffect::Kind::PatchShadow: {
                Color sc = fx.shadowFor(series_.resolvedColor());
                mfx.face = sc; mfx.edge = sc;
                break;
            }
            case PathEffect::Kind::Normal:
                drawMarkersAtPoints(cmd, r, axes, rect);
                break;
            }
            if (fx.kind != PathEffect::Kind::Normal)
                drawMarkersAtPoints(cmd, r, axes, rect, &mfx);
            if (fx.thenNormal)
                drawMarkersAtPoints(cmd, r, axes, rect);
        }
        return;
    }

    auto px = pixelPoints(series_, axes, rect, transform.get());
    // xkcd-style sketch wobble (path.sketch).
    if (axes.style().sketchScale > 0.0f)
        px = sketchPolyline(px, axes.style().sketchScale * 2.0f,
                            axes.style().sketchLength);

    StrokeParams sp;
    sp.width = series_.lineWidth;
    sp.dashes = series_.dashes.empty()
                    ? dashPattern(series_.lineStyle, series_.lineWidth)
                    : series_.dashes;
    sp.dashOffset = series_.dashOffset;
    sp.join = series_.joinStyle;
    sp.cap = series_.capStyle;

    // mpl clip_on=False → clip to the whole canvas, not the axes rect.
    auto eff = clipRect(rect, r.backend().extent());
    vk::Rect2D clip{vk::Offset2D{eff.x, eff.y},
                    vk::Extent2D{eff.width, eff.height}};
    vk::Extent2D res = r.backend().extent();
    auto& spine = r.spineRenderer();

    // Solid lines were tessellated on the GPU in preDraw; dashes and
    // patheffects passes fall back to the CPU stroker.
    auto linePass = [&](const StrokeParams& ps, Color color,
                        Point2D off) {
        std::vector<Point2D> pts;
        std::span<const Point2D> src = px;
        if (off.x != 0.0f || off.y != 0.0f) {
            pts.reserve(px.size());
            for (auto p : px) pts.push_back({p.x + off.x, p.y + off.y});
            src = pts;
        }
        auto mesh = strokePolyline(src, ps);
        spine.drawTriangles(cmd, clip, res, mesh.verts, color);
    };
    auto normalLine = [&] {
        // gapcolor: solid underlay first, dashed line on top.
        if (series_.gapColor.a > 0.0f && !sp.dashes.empty()) {
            StrokeParams solid = sp;
            solid.dashes.clear();
            linePass(solid, series_.gapColor, {});
        }
        if (gpuMeshSeq_ == r.frameSeq() && gpuMesh_.buffer) {
            spine.drawTrianglesGpu(cmd, clip, res, gpuMesh_.buffer,
                                   gpuMesh_.firstVertex * 6 * sizeof(float),
                                   gpuMesh_.vertexCount);
        } else {
            linePass(sp, series_.resolvedColor(), {});
        }
    };

    if (pathEffects.empty()) {
        normalLine();
        // Markers at each vertex (matplotlib plot marker=...).
        drawMarkersAtPoints(cmd, r, axes, rect);
        return;
    }

    // mpl patheffects: each entry produces its under-pass in list order;
    // Normal / thenNormal run the plain draw at that position.
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        MarkerFx mfx;
        mfx.offset = off;
        bool markerPass = true;
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            normalLine();
            drawMarkersAtPoints(cmd, r, axes, rect);
            markerPass = false;
            break;
        case PathEffect::Kind::Stroke: {
            // mpl Stroke: same path, gc overrides (linewidth/foreground).
            StrokeParams s2 = sp;
            s2.width = fx.strokeWidthPx(series_.lineWidth, dpi);
            Color fc = fx.foreground.value_or(series_.resolvedColor());
            if (fx.alpha) fc.a = *fx.alpha;
            linePass(s2, fc, off);
            mfx.edge = fc;
            mfx.edgeWidth = fx.strokeWidthPx(
                std::max(series_.markerEdgeWidth, 0.5f), dpi);
            break;
        }
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow: {
            // mpl shadows: same geometry offset, flat shadow color.
            Color sc = fx.shadowFor(series_.resolvedColor());
            linePass(sp, sc, off);
            mfx.face = sc;
            mfx.edge = sc;
            break;
        }
        }
        if (markerPass)
            drawMarkersAtPoints(cmd, r, axes, rect, &mfx);
        if (fx.thenNormal) {
            normalLine();
            drawMarkersAtPoints(cmd, r, axes, rect);
        }
    }
}

void LinePlot::drawMarkersAtPoints(vk::CommandBuffer cmd,
                                   render::Renderer& r,
                                   const Axes& axes, Rect2D rect,
                                   const MarkerFx* fx) {
    if (series_.size <= 0) return;
    auto eff = clipRect(rect, r.backend().extent());
    vk::Rect2D clip{vk::Offset2D{eff.x, eff.y},
                    vk::Extent2D{eff.width, eff.height}};
    Point2D off = fx ? fx->offset : Point2D{0.0f, 0.0f};
    std::vector<Point2D> px;
    px.reserve(series_.points.size());
    for (const auto& p : series_.points) {
        // Drop out-of-domain points (log/logit scales mask them).
        if (!transform &&
            (!pointInDomain(p, axes.xscale(), axes.yscale()) ||
             !std::isfinite(p.x) || !std::isfinite(p.y)))
            continue;
        Point2D q = transform ? transform->apply(p)
            : [&] {
                  auto f = axes.dataToFraction(p);
                  return Point2D{rect.x + f.x * float(rect.width),
                                 rect.y + (1.0f - f.y) * float(rect.height)};
              }();
        px.push_back({q.x + off.x, q.y + off.y});
    }
    if (series_.markevery) {
        auto idx = markerIndices(series_, px,
                                 std::hypot(float(rect.width),
                                            float(rect.height)));
        std::vector<Point2D> sub;
        sub.reserve(idx.size());
        for (size_t i : idx) sub.push_back(px[i]);
        px = std::move(sub);
    }
    const float mew = fx && fx->edgeWidth >= 0.0f
        ? fx->edgeWidth
        : std::max(series_.markerEdgeWidth, 0.5f);
    Color lineC = series_.resolvedColor();
    auto face = series_.applyAlpha(series_.markerFaceColor);
    auto edge = series_.applyAlpha(series_.markerEdgeColor);
    if (fx && fx->face) face = *fx->face;
    if (fx && fx->edge) edge = *fx->edge;
    if (series_.markerPath) {
        drawMarkersPx(r, cmd, clip, px, markerGeom(*series_.markerPath),
                      series_.size, lineC, mew, face, edge);
    } else if (!series_.markerTex.empty()) {
        drawTexMarkersPx(r, cmd, clip, px, series_.markerTex,
                         fx && fx->face ? *fx->face : lineC, series_.size);
    } else if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        // MarkerFill::None → mpl fillstyle='none' (unfilled marker).
        bool filled = series_.markerFill != MarkerFill::None;
        if (face && face->a == 0)
            filled = false;
        if (!filled) g.filled = false;
        drawMarkersPx(r, cmd, clip, px, g, series_.size, lineC,
                      mew, face, edge);
    }
}
void LinePlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (series_.points.empty()) return;
    const float dpi = axes.style().dpi;
    auto toPx = [&](Point2D p) {
        if (transform) return transform->apply(p);
        auto f = axes.dataToFraction(p);
        return Point2D{rect.x + f.x * float(rect.width),
                       rect.y + (1.0f - f.y) * float(rect.height)};
    };
    // Mask out-of-domain data (log/logit) — NaN points split polylines
    // and are skipped by emitMarkerAt. A custom transform bypasses the
    // data scales (mpl transform=).
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series_.points;
    if (!transform &&
        (axes.xscale().clipsDomain() || axes.yscale().clipsDomain())) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }

    auto shifted = [](std::span<const Point2D> pts, Point2D off) {
        std::vector<Point2D> out;
        out.reserve(pts.size());
        for (auto p : pts) out.push_back({p.x + off.x, p.y + off.y});
        return out;
    };
    render::VectorCanvas::Pen pen;
    pen.color = series_.resolvedColor();
    pen.width = series_.lineWidth;
    pen.dashes = series_.dashes.empty()
        ? dashPattern(series_.lineStyle, series_.lineWidth)
        : series_.dashes;
    pen.dashOffset = series_.dashOffset;
    pen.join = series_.joinStyle;
    pen.cap = series_.capStyle;

    // Marker geometry shared by all passes.
    MarkerGeom mg{};
    bool drawMarkers = series_.size > 0;
    const float mew = std::max(series_.markerEdgeWidth, 0.5f);
    if (drawMarkers) {
        if (series_.markerPath) mg = markerGeom(*series_.markerPath);
        else if (series_.marker != MarkerStyle::None)
            mg = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        else drawMarkers = false;
    }
    bool markerFilled = series_.markerFill != MarkerFill::None;
    auto faceOv = series_.applyAlpha(series_.markerFaceColor);
    auto edgeOv = series_.applyAlpha(series_.markerEdgeColor);
    if (faceOv && faceOv->a == 0) markerFilled = false;

    auto markerPositions = [&]() -> std::vector<Point2D> {
        std::vector<Point2D> mpx;
        mpx.reserve(src.size());
        for (const auto& dp : src) {
            auto p = toPx(dp);
            if (std::isfinite(p.x) && std::isfinite(p.y))
                mpx.push_back(p);
        }
        return mpx;
    };
    auto markerPass = [&](const std::vector<Point2D>& mpx, Point2D off,
                          std::optional<Color> face,
                          std::optional<Color> edge, float ew) {
        MarkerGeom g = mg;
        if (!markerFilled && !face) g.filled = false;
        for (auto p : mpx)
            emitMarkerPx(c, {p.x + off.x, p.y + off.y}, g, series_.size,
                         pen.color, ew,
                         face ? face : faceOv,
                         edge ? edge : edgeOv);
    };
    // mpl `markevery` subsamples the marker positions.
    std::vector<Point2D> mpx;
    if (drawMarkers) {
        mpx = markerPositions();
        if (series_.markevery) {
            auto idx = markerIndices(series_, mpx,
                std::hypot(float(rect.width), float(rect.height)));
            std::vector<Point2D> sub;
            for (size_t i : idx) sub.push_back(mpx[i]);
            mpx = std::move(sub);
        }
    }

    const bool hasLine = series_.lineStyle != LineStyle::None &&
                         series_.points.size() >= 2;
    std::vector<Point2D> px;
    if (hasLine) {
        auto pts = applyDrawStyle(src, series_.drawStyle);
        px.reserve(pts.size());
        for (const auto& p : pts) px.push_back(toPx(p));
        if (axes.style().sketchScale > 0.0f)
            px = sketchPolyline(px, axes.style().sketchScale * 2.0f,
                                axes.style().sketchLength);
    }
    auto normalLine = [&] {
        if (!hasLine) return;
        if (series_.gapColor.a > 0.0f && !pen.dashes.empty()) {
            auto solid = pen; solid.color = series_.gapColor;
            solid.dashes.clear();
            c.polyline(px, solid);
        }
        c.polyline(px, pen);
    };
    auto normalMarkers = [&] {
        if (drawMarkers)
            markerPass(mpx, {}, std::nullopt, std::nullopt, mew);
    };
    auto normalAll = [&] { normalLine(); normalMarkers(); };

    if (pathEffects.empty()) {
        // markerTex markers keep their text path.
        if (drawMarkers && !series_.markerTex.empty()) {
            normalLine();
            auto uni = text::mathTextToUnicode(series_.markerTex);
            const float halfW = series_.size * 0.3f * float(uni.size());
            for (auto p : mpx)
                c.text({p.x - halfW, p.y + series_.size * 0.35f},
                       uni, series_.size, pen.color);
            return;
        }
        normalAll();
        return;
    }

    // mpl patheffects: under-passes in list order.
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            normalAll();
            break;
        case PathEffect::Kind::Stroke: {
            Color fc = fx.foreground.value_or(pen.color);
            if (hasLine) {
                auto s2 = pen;
                s2.width = fx.strokeWidthPx(series_.lineWidth, dpi);
                s2.color = fc;
                c.polyline(shifted(px, off), s2);
            }
            if (drawMarkers)
                markerPass(mpx, off, faceOv, fc,
                           fx.strokeWidthPx(mew, dpi));
            break;
        }
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow: {
            Color sc = fx.shadowFor(pen.color);
            if (hasLine) {
                auto s2 = pen; s2.color = sc;
                c.polyline(shifted(px, off), s2);
            }
            if (drawMarkers)
                markerPass(mpx, off, sc, sc, mew);
            break;
        }
        }
        if (fx.thenNormal) normalAll();
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
