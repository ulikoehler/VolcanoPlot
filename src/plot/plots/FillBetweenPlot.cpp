// volcano/plot/plots/FillBetweenPlot.cpp
#include "volcano/plot/plots/FillBetweenPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "../VectorEmitHelpers.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>

namespace volcano::plot {

namespace {

/// mpl cbook.contiguous_regions: (start, end) index pairs covering the
/// contiguous true-runs of `mask`.
std::vector<std::pair<size_t, size_t>>
contiguousRegions(const std::vector<bool>& mask) {
    std::vector<std::pair<size_t, size_t>> out;
    const size_t n = mask.size();
    for (size_t i = 0; i < n;) {
        if (!mask[i]) { ++i; continue; }
        size_t j = i + 1;
        while (j < n && mask[j]) ++j;
        out.emplace_back(i, j);
        i = j;
    }
    return out;
}

/// The effective mpl fill mask: `where` (if given) AND-ed with the
/// finite mask of all three input arrays.
std::vector<bool> fillMask(const std::vector<float>& x,
                           const std::vector<float>& y1,
                           const std::vector<float>& y2,
                           const std::vector<bool>& where) {
    const size_t n = std::min({x.size(), y1.size(), y2.size()});
    std::vector<bool> mask(n, true);
    for (size_t i = 0; i < n; ++i)
        mask[i] = std::isfinite(x[i]) && std::isfinite(y1[i]) &&
                  std::isfinite(y2[i]);
    if (where.size() == n)
        for (size_t i = 0; i < n; ++i) mask[i] = mask[i] && where[i];
    return mask;
}

/// np.interp(x, xp, fp) for a 1- or 2-point sorted xp (clamped ends).
float interp1(float x, float x0, float x1, float f0, float f1) {
    if (x0 == x1) return x < x0 ? f0 : f1;
    float w = std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
    return f0 + w * (f1 - f0);
}

/// mpl FillBetweenPolyCollection._get_interpolating_points: the
/// interpolated f1==f2 crossing inside segment [idx-1, idx], returned
/// as the (x, f1) point on the upper curve.
Point2D crossingPoint(const std::vector<float>& x,
                      const std::vector<float>& y1,
                      const std::vector<float>& y2, size_t idx) {
    const size_t im1 = idx > 0 ? idx - 1 : 0;
    float d0 = y1[im1] - y2[im1], d1 = y1[idx] - y2[idx];
    float ta = x[im1], tb = x[idx];
    if (d0 > d1) { std::swap(d0, d1); std::swap(ta, tb); }
    float tc = interp1(0.0f, d0, d1, ta, tb);
    float fc = x[im1] <= x[idx]
                   ? interp1(tc, x[im1], x[idx], y1[im1], y1[idx])
                   : interp1(tc, x[idx], x[im1], y1[idx], y1[im1]);
    return {tc, fc};
}

/// Emit a quad as two triangles (upper-left, upper-right, lower-left /
/// upper-right, lower-right, lower-left).
void emitQuad(std::vector<Point2D>& outPos, std::vector<Color>& outColor,
              Color color, Point2D ul, Point2D ur, Point2D ll, Point2D lr) {
    outPos.insert(outPos.end(), {ul, ur, ll, ur, lr, ll});
    for (int j = 0; j < 6; ++j) outColor.push_back(color);
}

void emitTri(std::vector<Point2D>& outPos, std::vector<Color>& outColor,
             Color color, Point2D a, Point2D b, Point2D c) {
    outPos.insert(outPos.end(), {a, b, c});
    for (int j = 0; j < 3; ++j) outColor.push_back(color);
}

/// Triangle list for the fill between y1 (upper) and y2 (lower). With
/// `where`, each contiguous true-run is emitted separately; with
/// `interpolate` each run is extended to the y1==y2 crossing point at
/// its boundaries (mpl FillBetweenPolyCollection._make_verts_for_region).
void buildFillBetweenTriangles(const std::vector<float>& x,
                               const std::vector<float>& y1,
                               const std::vector<float>& y2,
                               const std::vector<bool>& where,
                               bool interpolate,
                               std::vector<Point2D>& outPos,
                               std::vector<Color>& outColor,
                               Color color) {
    const size_t n = std::min({x.size(), y1.size(), y2.size()});
    if (n < 2) return;
    auto mask = fillMask(x, y1, y2, where);
    for (auto [i0, i1] : contiguousRegions(mask)) {
        // An isolated True fills nothing (both adjacent segments are
        // False) — mpl documents the same where[i] && where[i+1] rule.
        if (i1 - i0 < 2) continue;
        for (size_t i = i0; i + 1 < i1; ++i)
            emitQuad(outPos, outColor, color,
                     {x[i], y1[i]}, {x[i + 1], y1[i + 1]},
                     {x[i], y2[i]}, {x[i + 1], y2[i + 1]});
        if (!interpolate) continue;
        if (i0 > 0) {
            Point2D p = crossingPoint(x, y1, y2, i0);
            emitTri(outPos, outColor, color, p,
                    {x[i0], y1[i0]}, {x[i0], y2[i0]});
        }
        if (i1 < n) {
            Point2D p = crossingPoint(x, y1, y2, i1);
            emitTri(outPos, outColor, color, p,
                    {x[i1 - 1], y1[i1 - 1]}, {x[i1 - 1], y2[i1 - 1]});
        }
    }
}

/// mpl fill geometry: each contiguous where-region becomes one
/// data-space polygon — [start, upper fwd, end, lower reversed].
/// Shared by the CPU raster path (patheffects/transform) and emitVector.
std::vector<std::vector<Point2D>>
fillPolygons(const std::vector<float>& x, const std::vector<float>& y1,
             const std::vector<float>& y2, const std::vector<bool>& where,
             bool interpolate) {
    const size_t n = std::min({x.size(), y1.size(), y2.size()});
    std::vector<std::vector<Point2D>> out;
    if (n < 2) return out;
    auto mask = fillMask(x, y1, y2, where);
    for (auto [i0, i1] : contiguousRegions(mask)) {
        if (i1 - i0 < 2) continue;
        Point2D start, end;
        if (interpolate) {
            start = crossingPoint(x, y1, y2, i0);
            end = crossingPoint(x, y1, y2, i1);
        } else {
            start = {x[i0], y2[i0]};
            end = {x[i1 - 1], y2[i1 - 1]};
        }
        std::vector<Point2D> poly;
        poly.reserve((i1 - i0) * 2 + 2);
        poly.push_back(start);
        for (size_t i = i0; i < i1; ++i) poly.push_back({x[i], y1[i]});
        poly.push_back(end);
        for (size_t i = i1; i-- > i0;) poly.push_back({x[i], y2[i]});
        if (poly.size() >= 3) out.push_back(std::move(poly));
    }
    return out;
}

} // namespace

void FillBetweenPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());

    // Build triangle list for the fill between area.
    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildFillBetweenTriangles(x_, y1_, y2_, where_, interpolate_,
                              positions, colors, color_);

    // Store the unique data points for GPU autoscale (the triangle vertices
    // include duplicates, so we build a separate list of unique points).
    uploadedPoints_.clear();
    uploadedPoints_.reserve(x_.size() * 2);  // upper + lower
    for (size_t i = 0; i < x_.size(); ++i) {
        uploadedPoints_.push_back({x_[i], y1_[i]});
        uploadedPoints_.push_back({x_[i], y2_[i]});
    }

    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{positions.data(), positions.size()},
                     std::span{colors.data(), colors.size()});
    prepared_ = true;
}


void FillBetweenPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                           const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    // mpl clip_on=False → clip to the whole canvas, not the axes rect.
    auto eff = clipRect(rect, r.backend().extent());
    vk::Rect2D vrect{vk::Offset2D{eff.x, eff.y},
                     vk::Extent2D{eff.width, eff.height}};

    if (!transform && pathEffects.empty()) {
        renderer_.draw(cmd, vrect, axes.transform());
        return;
    }

    // transform=/patheffects need pixel-space geometry (the GPU path
    // bakes data coords into the vertex buffer).
    const float dpi = axes.style().dpi;
    auto baseMap = pxMapper(axes, rect);
    auto toPx = [&](Point2D p) {
        return transform ? transform->apply(p) : baseMap(p);
    };
    auto& spine = r.spineRenderer();
    vk::Extent2D res = r.backend().extent();
    auto polys = fillPolygons(x_, y1_, y2_, where_, interpolate_);

    auto fillPass = [&](Color c, Point2D off) {
        for (const auto& poly : polys) {
            std::vector<Point2D> px;
            px.reserve(poly.size());
            for (auto p : poly)
                px.push_back({toPx(p).x + off.x, toPx(p).y + off.y});
            auto tris = earClip(px);
            if (!tris.empty())
                spine.drawTriangles(cmd, vrect, res, tris, c);
        }
    };
    auto edgePass = [&](Color c, float w, Point2D off) {
        for (const auto& poly : polys) {
            std::vector<Point2D> px;
            px.reserve(poly.size() + 1);
            for (auto p : poly)
                px.push_back({toPx(p).x + off.x, toPx(p).y + off.y});
            if (px.size() >= 3) {
                px.push_back(px.front());
                spine.drawLineStrip(cmd, vrect, res, px, c, w);
            }
        }
    };

    if (pathEffects.empty()) {
        fillPass(color_, {});
        return;
    }
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            fillPass(color_, {});
            break;
        case PathEffect::Kind::Stroke: {
            // mpl Stroke on a patch: fill keeps its color, the stroke
            // gets foreground/linewidth gc overrides.
            fillPass(color_, off);
            edgePass(fx.foreground.value_or(color_),
                     fx.strokeWidthPx(1.0f, dpi), off);
            break;
        }
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow:
            // mpl SimplePatchShadow: filled copy, lw=0, shadow color.
            fillPass(fx.shadowFor(color_), off);
            break;
        }
        if (fx.thenNormal) fillPass(color_, {});
    }
}

void FillBetweenPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                                 Rect2D rect) {
    const float dpi = axes.style().dpi;
    auto baseMap = pxMapper(axes, rect);
    auto toPx = [&](Point2D p) {
        return transform ? transform->apply(p) : baseMap(p);
    };
    auto polys = fillPolygons(x_, y1_, y2_, where_, interpolate_);
    if (polys.empty()) return;

    auto pxPolys = [&](Point2D off) {
        std::vector<std::vector<Point2D>> out;
        out.reserve(polys.size());
        for (const auto& poly : polys) {
            std::vector<Point2D> px;
            px.reserve(poly.size());
            for (auto p : poly)
                px.push_back({toPx(p).x + off.x, toPx(p).y + off.y});
            out.push_back(std::move(px));
        }
        return out;
    };
    auto fillPass = [&](Color col, Point2D off) {
        for (const auto& px : pxPolys(off)) c.polygon(px, col);
    };
    auto edgePass = [&](Color col, float w, Point2D off) {
        render::VectorCanvas::Pen pen;
        pen.color = col; pen.width = w;
        for (auto px : pxPolys(off)) {
            px.push_back(px.front());
            c.polyline(px, pen);
        }
    };
    if (pathEffects.empty()) {
        fillPass(color_, {});
        return;
    }
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            fillPass(color_, {});
            break;
        case PathEffect::Kind::Stroke:
            fillPass(color_, off);
            edgePass(fx.foreground.value_or(color_),
                     fx.strokeWidthPx(1.0f, dpi), off);
            break;
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow:
            fillPass(fx.shadowFor(color_), off);
            break;
        }
        if (fx.thenNormal) fillPass(color_, {});
    }
}

void FillBetweenPlot::contributeToAutoscale(Viewport& v) const {
    const size_t n = std::min({x_.size(), y1_.size(), y2_.size()});
    // mpl datalim: only the where-selected (finite) points count.
    auto mask = fillMask(x_, y1_, y2_, where_);
    for (size_t i = 0; i < n; ++i) {
        if (!mask[i]) continue;
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y1_[i]);
        v.y.max = std::max(v.y.max, y1_[i]);
        v.y.min = std::min(v.y.min, y2_[i]);
        v.y.max = std::max(v.y.max, y2_[i]);
    }
}

void FillBetweenPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    // Use the FillRenderer's point buffer (which contains triangle vertices,
    // but min/max over those is the same as min/max over the data points).
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                    renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
