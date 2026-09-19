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

void ScatterPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.descriptorPool(), r.pipelineCache());

    std::vector<Color> colors(series_.points.size(), series_.resolvedColor());
    std::vector<float> sizes(series_.points.size(), series_.size);
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, std::span{colors}, std::span{sizes});
    prepared_ = true;
}

void ScatterPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};

    // Custom Path / TeX markers, and domain-limited scales (log/logit
    // masks out-of-domain points), bypass the SDF point pipeline and
    // draw markers CPU-side so invalid points can be dropped.
    bool clip = axes.xscale().clipsDomain() || axes.yscale().clipsDomain();
    if (series_.markerPath || !series_.markerTex.empty() || clip) {
        std::vector<Point2D> px;
        px.reserve(series_.points.size());
        for (const auto& p : series_.points) {
            if (!pointInDomain(p, axes.xscale(), axes.yscale()) ||
                !std::isfinite(p.x) || !std::isfinite(p.y))
                continue;
            auto f = axes.dataToFraction(p);
            px.push_back({rect.x + f.x * float(rect.width),
                          rect.y + (1.0f - f.y) * float(rect.height)});
        }
        if (series_.markerPath)
            drawMarkersPx(r, cmd, vrect, px,
                          markerGeom(*series_.markerPath), series_.size,
                          series_.resolvedColor(), std::max(1.0f, series_.size * 0.1f));
        else if (!series_.markerTex.empty())
            drawTexMarkersPx(r, cmd, vrect, px, series_.markerTex,
                             series_.resolvedColor(), series_.size);
        else
            drawMarkersPx(r, cmd, vrect, px,
                          markerGeom(series_.marker, series_.markerNumsides,
                                     series_.markerAngle),
                          series_.size, series_.resolvedColor(),
                          std::max(1.0f, series_.size * 0.1f));
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
    auto toPx = pxMapper(axes, rect);
    // Mask out-of-domain data (log/logit); NaN points are skipped by
    // emitMarkerAt.
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series_.points;
    if (axes.xscale().clipsDomain() || axes.yscale().clipsDomain()) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
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
    emitMarkerAt(c, toPx, src, g,
                 series_.size, fill ? series_.resolvedColor() : Color::transparent(),
                 std::max(1.0f, series_.size * 0.1f));
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
