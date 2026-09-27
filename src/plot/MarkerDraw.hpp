// src/plot/MarkerDraw.hpp — shared raster marker drawing for Series2D
// plots (scatter/line markers via SpineRenderer tessellation).
#pragma once

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Path.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"

namespace volcano::plot {

/// Raster-draw marker geometry centered at each pixel position.
/// `size` is the marker diameter in px. `fill` controls whether the
/// outlines are filled or stroked (MarkerFill::None → stroked).
/// mpl markerfacecolor/markeredgecolor: nullopt → `color`; a transparent
/// Color → 'none' (no fill / no edge); opaque → explicit color.
/// `colors`/`sizes` give per-point overrides (mpl scatter c=/s= arrays);
/// empty spans draw uniformly with `color`/`size`.
inline void drawMarkersPx(render::Renderer& r, vk::CommandBuffer cmd,
                          vk::Rect2D clip, std::span<const Point2D> pxPts,
                          const MarkerGeom& g, float size, Color color,
                          float strokeW = 1.0f,
                          const std::optional<Color>& face = std::nullopt,
                          const std::optional<Color>& edge = std::nullopt,
                          std::span<const Color> colors = {},
                          std::span<const float> sizes = {},
                          const std::optional<render::primitives::MarkerParams>& sdf =
                              std::nullopt) {
    if (size <= 0.0f) return;
    auto& spine = r.spineRenderer();
    auto res = r.backend().extent();
    // mpl: a stroked outline is drawn when the marker isn't filled or an
    // explicit edge color was given; 'none' (a==0) suppresses it.
    bool strokesPossible = !g.filled || (edge.has_value() && edge->a > 0);

    if (!strokesPossible && g.filled && g.strokes.empty() &&
        !g.outlines.empty()) {
        if (sdf && pxPts.size() >= 32) {
            // SDF point-sprite path — one vertex per marker instead of a
            // per-marker tessellated triangle soup (~57 verts for 'o').
            // Pure fills only: the sprites can't draw a distinct edge
            // color, so stroke-needing markers use the paths below.
            auto& ctx = r.backend().context();
            auto& pr = r.pointRenderer();
            const size_t n = pxPts.size();
            std::vector<Color> colScratch;
            std::span<const Color> cols;
            if (!face && colors.size() >= n) {
                cols = {colors.data(), n};
            } else {
                colScratch.assign(n, face.value_or(color));
                cols = colScratch;
            }
            std::vector<float> szScratch;
            std::span<const float> sz = sizes;
            if (sz.size() < n) {
                szScratch.resize(n);
                for (size_t i = 0; i < n; ++i)
                    szScratch[i] = i < sz.size() ? sz[i] : size;
                sz = szScratch;
            }
            if (pr.hasData())
                pr.updatePoints(pxPts, cols, sz);
            else
                pr.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                          ctx.graphicsPool.handle(), ctx.allocator.handle(),
                          pxPts, cols, sz);
            // Canvas-pixel view (same convention as MplCanvas::execPoints):
            // x spans [0,W], y is reversed [H,0] so canvas Y-down lands
            // correctly after the shader's Y-flip.
            Transform2D t;
            t.view.x = {0.0f, static_cast<float>(res.width)};
            t.view.y = {static_cast<float>(res.height), 0.0f};
            vk::Rect2D vp{vk::Offset2D{0, 0}, res};
            pr.draw(cmd, vp, clip, t, static_cast<uint32_t>(n), *sdf);
            return;
        }
        // Fast path — pure filled markers: earClip each unit outline
        // ONCE, then tile scaled+translated copies into one triangle
        // soup → a single drawTriangles call for the whole series
        // (per-marker earClip+alloc dominated large marker sets).
        std::vector<std::vector<Point2D>> unitTris;
        size_t tvPer = 0;
        for (const auto& out : g.outlines) {
            if (out.size() < 3) continue;
            auto t = earClip(out);
            tvPer += t.size();
            unitTris.push_back(std::move(t));
        }
        if (tvPer == 0) return;
        std::vector<Point2D> tris;
        std::vector<Color> triCols;
        tris.reserve(pxPts.size() * tvPer);
        bool perCol = !colors.empty();
        if (perCol) triCols.reserve(pxPts.size() * tvPer);
        for (size_t i = 0; i < pxPts.size(); ++i) {
            const auto& c0 = pxPts[i];
            float sz = i < sizes.size() ? sizes[i] : size;
            if (sz <= 0.0f) continue;
            Color fc = face.value_or(i < colors.size() ? colors[i]
                                                       : color);
            if (fc.a <= 0.0f) continue;
            for (auto& ut : unitTris)
                for (auto q : ut) {
                    tris.push_back({c0.x + q.x * sz, c0.y + q.y * sz});
                    if (perCol) triCols.push_back(fc);
                }
        }
        if (!tris.empty()) {
            if (perCol)
                spine.drawTrianglesVC(cmd, clip, res, tris, triCols);
            else
                spine.drawTriangles(cmd, clip, res, tris,
                                    face.value_or(color));
        }
        return;
    }

    for (size_t i = 0; i < pxPts.size(); ++i) {
        const auto& c0 = pxPts[i];
        Color base = i < colors.size() ? colors[i] : color;
        Color fc = face.value_or(base);
        Color ec = edge.value_or(base);
        float sz = i < sizes.size() ? sizes[i] : size;
        if (sz <= 0.0f) continue;
        bool strokeOutline = !g.filled || edge.has_value();
        auto scaled = [&](const std::vector<Point2D>& v) {
            std::vector<Point2D> out;
            out.reserve(v.size());
            for (auto q : v)
                out.push_back({c0.x + q.x * sz, c0.y + q.y * sz});
            return out;
        };
        for (const auto& out : g.outlines) {
            if (out.size() < 3) continue;
            auto ring = scaled(out);
            if (g.filled && fc.a > 0.0f)
                spine.drawTriangles(cmd, clip, res, earClip(ring), fc);
            if (strokeOutline && ec.a > 0.0f) {
                ring.push_back(ring.front());
                spine.drawLineStrip(cmd, clip, res, ring, ec, strokeW);
            }
        }
        for (const auto& st : g.strokes) {
            auto line = scaled(st);
            if (line.size() >= 2 && ec.a > 0.0f)
                spine.drawLineStrip(cmd, clip, res, line, ec, strokeW);
        }
    }
}

/// Raster-draw a TeX/mathtext marker string centered at each pixel
/// position (matplotlib marker='$…$'). `size` scales the glyph.
inline void drawTexMarkersPx(render::Renderer& r, vk::CommandBuffer cmd,
                             vk::Rect2D clip,
                             std::span<const Point2D> pxPts,
                             std::string_view tex, Color color,
                             float size) {
    if (!r.textReady() || tex.empty() || size <= 0.0f) return;
    const float scale = size / 16.0f;
    // markerTex stores the inner mathtext — wrap in $...$ so the rich
    // text path parses it (matplotlib marker='$…$').
    std::string wrapped = "$" + std::string(tex) + "$";
    auto m = r.measureRichText(wrapped, scale);
    for (const auto& c0 : pxPts)
        // Center the glyph block on the point: baseline is top-left +
        // ascent; shift left by half width, up by half height.
        r.drawRichText(cmd, clip, wrapped, c0.x - m.width / 2.0f,
                       c0.y - m.height / 2.0f + m.ascent, color, scale);
}

} // namespace volcano::plot
