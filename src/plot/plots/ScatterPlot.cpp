// volcano/plot/plots/ScatterPlot.cpp
#include "volcano/plot/plots/ScatterPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include "volcano/text/MathText.hpp"
#include "../MarkerDraw.hpp"
#include "../VectorEmitHelpers.hpp"

#include <algorithm>
#include <limits>

namespace volcano::plot {

void ScatterPlot::setData(std::vector<float> x, std::vector<float> y) {
    const size_t n = std::min(x.size(), y.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i)
        series_.points[i] = {x[i], y[i]};
    dataDirty_ = true;
    touch();
}
void ScatterPlot::setOffsets(std::vector<Point2D> points) {
    series_.points = std::move(points);
    dataDirty_ = true;
    touch();
}

/// mpl scatter `c=<array>` colormapping — per-point colors through
/// `cmap_`+`norm_`, or the explicit `colors_` list. Empty return means
/// "uniform series color" (no colormapping configured).
std::vector<Color> ScatterPlot::pointColors() const {
    const size_t n = series_.points.size();
    std::vector<Color> colors;
    if (!colors_.empty()) {
        colors.assign(n, series_.resolvedColor());
        for (size_t i = 0; i < n && i < colors_.size(); ++i)
            colors[i] = colors_[i];
        for (auto& c : colors) c.a *= series_.alpha;
        return colors;
    }
    if (array_.empty()) return colors;
    ensureNorm();
    norm_->autoscale(array_);
    colors.resize(n);
    for (size_t i = 0; i < n; ++i) {
        float v = i < array_.size() ? array_[i] : 0.0f;
        Color c = cmap_->sample((*norm_)(v));
        c.a *= series_.alpha;
        colors[i] = c;
    }
    return colors;
}

/// Per-point marker diameters: sizes_ where provided, series_.size
/// elsewhere (mpl scatter s=<array> broadcast behavior).
std::vector<float> ScatterPlot::pointSizes() const {
    const size_t n = series_.points.size();
    std::vector<float> sizes(n, series_.size);
    for (size_t i = 0; i < n && i < sizes_.size(); ++i)
        sizes[i] = sizes_[i];
    return sizes;
}

void ScatterPlot::setClim(std::optional<float> vmin,
                          std::optional<float> vmax) {
    ensureNorm();
    norm_->setVmin(vmin.value_or(std::nanf("")));
    norm_->setVmax(vmax.value_or(std::nanf("")));
    touch();
}

std::optional<Range> ScatterPlot::valueRange() const {
    if (array_.empty()) return {};
    ensureNorm();
    norm_->autoscale(array_);
    return Range{norm_->vmin(), norm_->vmax()};
}

void ScatterPlot::prepare(render::Renderer& r) {
    if (prepared_) {
        // In-place update via memcpy (reallocs only on growth) — direct
        // series() writes stay correct, no dirty flag to bypass.
        auto colors = pointColors();
        if (colors.empty())
            colors.assign(series_.points.size(), series_.resolvedColor());
        auto sizes = pointSizes();
        renderer_.updatePoints(std::span{series_.points},
                               std::span{colors}, std::span{sizes});
        dataDirty_ = false;
        return;
    }
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.descriptorPool(), r.pipelineCache());

    auto colors = pointColors();
    if (colors.empty())
        colors.assign(series_.points.size(), series_.resolvedColor());
    auto sizes = pointSizes();
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, std::span{colors}, std::span{sizes});
    prepared_ = true;
    dataDirty_ = false;
}

void ScatterPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    // mpl clip_on=False → clip to the whole canvas, not the axes rect.
    auto eff = clipRect(rect, r.backend().extent());
    vk::Rect2D vrect{vk::Offset2D{eff.x, eff.y}, vk::Extent2D{eff.width, eff.height}};

    // Custom Path / TeX markers, domain-limited scales (log/logit
    // masks out-of-domain points), artist transforms (mpl transform=),
    // explicit marker edge colors (mpl edgecolors=), and path effects
    // bypass the SDF point pipeline and draw markers CPU-side so
    // invalid points can be dropped, edges stroked, and passes
    // recolored.
    bool clip = axes.xscale().clipsDomain() || axes.yscale().clipsDomain();
    bool hasEdge = series_.markerEdgeColor &&
                   series_.markerEdgeColor->a > 0.0f;
    if (series_.markerPath || !series_.markerTex.empty() || clip ||
        transform || !pathEffects.empty() || hasEdge) {
        const float dpi = axes.style().dpi;
        std::vector<Point2D> px;
        std::vector<size_t> kept;  // source index per kept point
        px.reserve(series_.points.size());
        for (size_t i = 0; i < series_.points.size(); ++i) {
            const auto& p = series_.points[i];
            if (!transform &&
                (!pointInDomain(p, axes.xscale(), axes.yscale()) ||
                 !std::isfinite(p.x) || !std::isfinite(p.y)))
                continue;
            Point2D q = transform ? transform->apply(p)
                : [&] {
                      auto f = axes.dataToFraction(p);
                      return Point2D{
                          rect.x + f.x * float(rect.width),
                          rect.y + (1.0f - f.y) * float(rect.height)};
                  }();
            px.push_back(q);
            kept.push_back(i);
        }
        // Per-point c=/s= arrays, filtered to the kept points.
        pcCache_.clear();
        psCache_.clear();
        if (!array_.empty() || !colors_.empty() || !sizes_.empty()) {
            auto allC = pointColors();
            auto allS = pointSizes();
            pcCache_.reserve(kept.size());
            psCache_.reserve(kept.size());
            for (size_t i : kept) {
                if (!allC.empty()) pcCache_.push_back(allC[i]);
                psCache_.push_back(allS[i]);
            }
        }
        const float mew = std::max(series_.markerEdgeWidth, 0.5f);
        auto normalPass = [&](Point2D off) {
            if (off.x || off.y)
                for (auto& p : px) { p.x += off.x; p.y += off.y; }
            if (series_.markerPath)
                drawMarkersPx(r, cmd, vrect, px,
                              markerGeom(*series_.markerPath), series_.size,
                              series_.resolvedColor(), mew,
                              series_.applyAlpha(series_.markerFaceColor),
                              series_.applyAlpha(series_.markerEdgeColor),
                              pcCache_, psCache_);
            else if (!series_.markerTex.empty())
                drawTexMarkersPx(r, cmd, vrect, px, series_.markerTex,
                                 series_.resolvedColor(), series_.size);
            else {
                auto g = markerGeom(series_.marker, series_.markerNumsides,
                                    series_.markerAngle);
                if (series_.markerFill == MarkerFill::None ||
                    (series_.applyAlpha(series_.markerFaceColor) && series_.applyAlpha(series_.markerFaceColor)->a == 0))
                    g.filled = false;
                drawMarkersPx(r, cmd, vrect, px, g, series_.size,
                              series_.resolvedColor(), mew,
                              series_.applyAlpha(series_.markerFaceColor),
                              series_.applyAlpha(series_.markerEdgeColor),
                              pcCache_, psCache_);
            }
        };
        if (pathEffects.empty()) {
            normalPass({});
            return;
        }
        for (const auto& fx : pathEffects) {
            Point2D off = fx.offsetPx(dpi);
            std::optional<Color> faceOv, edgeOv;
            float ew = mew;
            switch (fx.kind) {
            case PathEffect::Kind::Normal:
                normalPass({});
                continue;
            case PathEffect::Kind::Stroke:
                edgeOv = fx.foreground.value_or(series_.resolvedColor());
                ew = fx.strokeWidthPx(mew, dpi);
                break;
            case PathEffect::Kind::LineShadow:
            case PathEffect::Kind::PatchShadow: {
                Color sc = fx.shadowFor(series_.resolvedColor());
                faceOv = sc;
                edgeOv = sc;
                break;
            }
            }
            // Offset marker pass with overridden colors.
            std::vector<Point2D> opx = px;
            for (auto& p : opx) { p.x += off.x; p.y += off.y; }
            if (series_.markerPath) {
                drawMarkersPx(r, cmd, vrect, opx,
                              markerGeom(*series_.markerPath), series_.size,
                              series_.resolvedColor(), ew,
                              faceOv ? faceOv : series_.applyAlpha(series_.markerFaceColor),
                              edgeOv ? edgeOv : series_.applyAlpha(series_.markerEdgeColor),
                              {}, psCache_);
            } else if (!series_.markerTex.empty()) {
                drawTexMarkersPx(r, cmd, vrect, opx, series_.markerTex,
                                 faceOv.value_or(series_.resolvedColor()),
                                 series_.size);
            } else {
                auto g = markerGeom(series_.marker, series_.markerNumsides,
                                    series_.markerAngle);
                if (series_.markerFill == MarkerFill::None ||
                    (series_.applyAlpha(series_.markerFaceColor) && series_.applyAlpha(series_.markerFaceColor)->a == 0))
                    g.filled = false;
                drawMarkersPx(r, cmd, vrect, opx, g, series_.size,
                              series_.resolvedColor(), ew,
                              faceOv ? faceOv : series_.applyAlpha(series_.markerFaceColor),
                              edgeOv ? edgeOv : series_.applyAlpha(series_.markerEdgeColor),
                              {}, psCache_);
            }
            if (fx.thenNormal) normalPass({});
        }
        return;
    }

    Transform2D t = axes.transform();
    render::primitives::MarkerParams mp{
        .code = static_cast<float>(static_cast<int>(series_.marker)),
        .fill = static_cast<float>(static_cast<int>(series_.markerFill)),
        .numsides = static_cast<float>(series_.markerNumsides),
        .angle = series_.markerAngle,
    };
    renderer_.draw(cmd, vrect, t,
                   static_cast<uint32_t>(series_.points.size()), mp);
}

void ScatterPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                             Rect2D rect) {
    const float dpi = axes.style().dpi;
    auto baseMapper = pxMapper(axes, rect);
    auto toPx = [&](Point2D p) -> Point2D {
        return transform ? transform->apply(p) : baseMapper(p);
    };
    // Mask out-of-domain data (log/logit); NaN points are skipped by
    // emitMarkerAt. Custom transforms bypass the data scales.
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series_.points;
    std::vector<size_t> kept;  // source index per kept point
    if (!transform &&
        (axes.xscale().clipsDomain() || axes.yscale().clipsDomain())) {
        for (size_t i = 0; i < src.size(); ++i)
            if (pointInDomain(src[i], axes.xscale(), axes.yscale())) {
                masked.push_back(src[i]);
                kept.push_back(i);
            }
        src = masked;
    }
    // Per-point c=/s= arrays aligned to the (masked) emitted points.
    pcCache_.clear();
    psCache_.clear();
    if (!array_.empty() || !colors_.empty() || !sizes_.empty()) {
        auto allC = pointColors();
        auto allS = pointSizes();
        pcCache_.reserve(src.size());
        psCache_.reserve(src.size());
        if (kept.empty()) {
            pcCache_ = allC;
            psCache_ = allS;
        } else {
            for (size_t i : kept) {
                if (!allC.empty()) pcCache_.push_back(allC[i]);
                psCache_.push_back(allS[i]);
            }
        }
    }
    // TeX marker: emit the Unicode-flattened glyph string centered per
    // point (approximate centering — writers lack font metrics).
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
    auto g = series_.markerPath
        ? markerGeom(*series_.markerPath)
        : markerGeom(series_.marker, series_.markerNumsides,
                     series_.markerAngle);
    bool fill = series_.markerFill != MarkerFill::None;
    Color baseC = fill ? series_.resolvedColor() : Color::transparent();
    float mew = std::max(1.0f, series_.size * 0.1f);
    // Per-point colors feed the marker face unless an explicit
    // markerfacecolor overrides (mpl scatter c= maps the face).
    std::optional<Color> faceOv = series_.applyAlpha(series_.markerFaceColor);
    auto pass = [&](Point2D off, Color fc, std::optional<Color> edge,
                    float ew) {
        auto offPx = [&](Point2D p) {
            auto q = toPx(p);
            return Point2D{q.x + off.x, q.y + off.y};
        };
        emitMarkerAt(c, offPx, src, g, series_.size, fc, ew,
                     std::nullopt, edge, {}, psCache_);
    };
    auto passColored = [&](Point2D off, std::optional<Color> edge,
                           float ew) {
        auto offPx = [&](Point2D p) {
            auto q = toPx(p);
            return Point2D{q.x + off.x, q.y + off.y};
        };
        emitMarkerAt(c, offPx, src, g, series_.size, baseC, ew,
                     faceOv, edge, pcCache_, psCache_);
    };
    if (pathEffects.empty()) {
        // mpl: explicit edgecolors= strokes each marker at linewidths.
        if (series_.markerEdgeColor &&
            series_.markerEdgeColor->a > 0.0f)
            passColored({}, series_.applyAlpha(series_.markerEdgeColor),
                        std::max(series_.markerEdgeWidth, 0.5f));
        else
            passColored({}, std::nullopt, mew);
        return;
    }
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            passColored({}, std::nullopt, mew);
            break;
        case PathEffect::Kind::Stroke:
            pass(off, baseC,
                 fx.foreground.value_or(series_.resolvedColor()),
                 fx.strokeWidthPx(mew, dpi));
            break;
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow: {
            Color sc = fx.shadowFor(series_.resolvedColor());
            pass(off, sc, sc, mew);
            break;
        }
        }
        if (fx.thenNormal) passColored({}, std::nullopt, mew);
    }
}
void ScatterPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x);
        v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y);
        v.y.max = std::max(v.y.max, p.y);
    }
}

void ScatterPlot::contributeToAutoscaleScaled(Viewport& v,
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

void ScatterPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(), renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
