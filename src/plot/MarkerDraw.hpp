// src/plot/MarkerDraw.hpp — shared raster marker drawing for Series2D
// plots (scatter/line markers via SpineRenderer tessellation).
#pragma once

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Path.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"

namespace volcano::plot {

/// Raster-draw marker geometry centered at each pixel position.
/// `size` is the marker diameter in px. `fill` controls whether the
/// outlines are filled or stroked (MarkerFill::None → stroked).
inline void drawMarkersPx(render::Renderer& r, vk::CommandBuffer cmd,
                          vk::Rect2D clip, std::span<const Point2D> pxPts,
                          const MarkerGeom& g, float size, Color color,
                          float strokeW = 1.0f) {
    if (size <= 0.0f) return;
    auto& spine = r.spineRenderer();
    auto res = r.backend().extent();
    for (const auto& c0 : pxPts) {
        auto scaled = [&](const std::vector<Point2D>& v) {
            std::vector<Point2D> out;
            out.reserve(v.size());
            for (auto q : v)
                out.push_back({c0.x + q.x * size, c0.y + q.y * size});
            return out;
        };
        for (const auto& out : g.outlines) {
            if (out.size() < 3) continue;
            auto ring = scaled(out);
            if (g.filled) {
                spine.drawTriangles(cmd, clip, res, earClip(ring), color);
            } else {
                ring.push_back(ring.front());
                spine.drawLineStrip(cmd, clip, ring, color, strokeW);
            }
        }
        for (const auto& st : g.strokes) {
            auto line = scaled(st);
            if (line.size() >= 2)
                spine.drawLineStrip(cmd, clip, line, color, strokeW);
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
    auto m = r.measureRichText(tex, scale);
    for (const auto& c0 : pxPts)
        // Center the glyph block on the point: baseline is top-left +
        // ascent; shift left by half width, up by half height.
        r.drawRichText(cmd, clip, tex, c0.x - m.width / 2.0f,
                       c0.y - m.height / 2.0f + m.ascent, color, scale);
}

} // namespace volcano::plot
