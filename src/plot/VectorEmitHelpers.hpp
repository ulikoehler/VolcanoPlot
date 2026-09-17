// src/plot/VectorEmitHelpers.hpp — shared helpers for IPlot::emitVector
// implementations (data→pixel mapping and marker emission).
#pragma once

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/render/VectorCanvas.hpp"

namespace volcano::plot {

/// Data-space → figure-pixel mapper for an axes rect.
inline auto pxMapper(const Axes& axes, Rect2D rect) {
    return [&axes, rect](Point2D p) {
        auto f = axes.dataToFraction(p);
        return Point2D{rect.x + f.x * float(rect.width),
                       rect.y + (1.0f - f.y) * float(rect.height)};
    };
}

/// Emit a marker at each pixel position. `size` is the marker diameter
/// in px; stroke markers use `strokeW` for the pen width. A transparent
/// `color` strokes the outline instead of filling (MarkerFill::None).
template <class Map>
void emitMarkerAt(render::VectorCanvas& c, const Map& toPx,
                  std::span<const Point2D> pts, const MarkerGeom& g,
                  float size, Color color, float strokeW = 1.0f) {
    render::VectorCanvas::Pen pen;
    pen.color = color;
    pen.width = strokeW;
    for (const auto& dp : pts) {
        Point2D c0 = toPx(dp);
        auto scaled = [&](std::span<const Point2D> v) {
            std::vector<Point2D> out;
            out.reserve(v.size());
            for (auto q : v)
                out.push_back({c0.x + q.x * size, c0.y + q.y * size});
            return out;
        };
        for (const auto& out : g.outlines) {
            if (out.size() < 3) continue;
            if (g.filled && color.a > 0.0f) {
                c.polygon(scaled(out), color);
            } else {
                auto ring = scaled(out);
                ring.push_back(ring.front());
                c.polyline(ring, pen);
            }
        }
        for (const auto& st : g.strokes) {
            auto line = scaled(st);
            if (line.size() >= 2) c.polyline(line, pen);
        }
    }
}

/// Emit a marker at one pixel position directly.
inline void emitMarkerPx(render::VectorCanvas& c, Point2D c0,
                         const MarkerGeom& g, float size, Color color,
                         float strokeW = 1.0f) {
    Point2D at[1] = {{0, 0}};
    emitMarkerAt(c, [c0](Point2D) { return c0; }, at, g, size, color,
                 strokeW);
}

} // namespace volcano::plot
