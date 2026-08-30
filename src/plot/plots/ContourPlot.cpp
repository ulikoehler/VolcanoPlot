// volcano/plot/plots/ContourPlot.cpp — contour and contourf implementation
#include "volcano/plot/plots/ContourPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {

// ─── Marching Squares Lookup Table ──────────────────────────────────────────
//
// Cell corner layout:
//   TL --- TR        3 --- 2
//    |       |        |       |
//   BL --- BR        0 --- 1
//
// Edge layout:
//   Edge 0: bottom (BL → BR)
//   Edge 1: right  (BR → TR)
//   Edge 2: top    (TR → TL)
//   Edge 3: left   (TL → BL)
//
// For a given level L, the 4-bit code is:
//   bit 0: BL >= L, bit 1: BR >= L, bit 2: TR >= L, bit 3: TL >= L
//
// For each case (0–15), the table gives pairs of edge indices to
// interpolate. -1 means no more segments.

struct EdgePair { int e0, e1; };

// Non-saddle cases (and saddle cases resolved with center < level).
constexpr EdgePair kMarchTable[16] = {
    {-1, -1},  //  0: none above
    { 3,  0},  //  1: BL
    { 0,  1},  //  2: BR
    { 3,  1},  //  3: BL, BR
    { 1,  2},  //  4: TR
    { 3,  2},  //  5: BL, TR (saddle, center below)
    { 0,  2},  //  6: BR, TR
    { 3,  2},  //  7: BL, BR, TR
    { 2,  3},  //  8: TL
    { 0,  2},  //  9: BL, TL
    { 0,  3},  // 10: BR, TL (saddle, center below)
    { 1,  2},  // 11: BL, BR, TL
    { 1,  3},  // 12: TR, TL
    { 0,  1},  // 13: BL, TR, TL
    { 0,  3},  // 14: BR, TR, TL
    {-1, -1},  // 15: all above
};

// Saddle alternatives (center >= level merges the above-regions).
constexpr EdgePair kMarchSaddleAbove[16] = {
    {-1, -1},  //  0
    { 3,  0},  //  1
    { 0,  1},  //  2
    { 3,  1},  //  3
    { 1,  2},  //  4
    { 0,  1},  //  5: BL, TR (saddle, center above) → connect 0-1, 2-3
    { 0,  2},  //  6
    { 3,  2},  //  7
    { 2,  3},  //  8
    { 0,  2},  //  9
    { 1,  2},  // 10: BR, TL (saddle, center above) → connect 1-2, 0-3
    { 1,  2},  // 11
    { 1,  3},  // 12
    { 0,  1},  // 13
    { 0,  3},  // 14
    {-1, -1},  // 15
};

// Only cases 5 and 10 are saddles.
constexpr bool isSaddle(int code) { return code == 5 || code == 10; }

/// Interpolate the position where `level` crosses an edge of the cell.
/// edge 0=bottom, 1=right, 2=top, 3=left.
Point2D interpEdge(int edge, float level,
                   float x0, float y0, float x1, float y1,
                   float vBL, float vBR, float vTR, float vTL) {
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
    switch (edge) {
    case 0: { // bottom: BL→BR
        float t = (level - vBL) / (vBR - vBL);
        return {lerp(x0, x1, t), y0};
    }
    case 1: { // right: BR→TR
        float t = (level - vBR) / (vTR - vBR);
        return {x1, lerp(y0, y1, t)};
    }
    case 2: { // top: TR→TL
        float t = (level - vTR) / (vTL - vTR);
        return {lerp(x1, x0, t), y1};
    }
    default: { // left: TL→BL
        float t = (level - vTL) / (vBL - vTL);
        return {x0, lerp(y1, y0, t)};
    }
    }
}

/// Sutherland-Hodgman polygon clipping vertex with associated scalar value.
struct ClipVertex { Point2D pos; float val; };

/// Clip polygon to keep vertices with val >= level.
std::vector<ClipVertex> clipAbove(std::span<const ClipVertex> poly, float level) {
    std::vector<ClipVertex> out;
    if (poly.empty()) return out;
    auto n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& cur = poly[i];
        const auto& nxt = poly[(i + 1) % n];
        bool curIn = cur.val >= level;
        bool nxtIn = nxt.val >= level;
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) {
            float t = (level - cur.val) / (nxt.val - cur.val);
            out.push_back({{
                cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                cur.pos.y + t * (nxt.pos.y - cur.pos.y)
            }, level});
        }
    }
    return out;
}

/// Clip polygon to keep vertices with val <= level.
std::vector<ClipVertex> clipBelow(std::span<const ClipVertex> poly, float level) {
    std::vector<ClipVertex> out;
    if (poly.empty()) return out;
    auto n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& cur = poly[i];
        const auto& nxt = poly[(i + 1) % n];
        bool curIn = cur.val <= level;
        bool nxtIn = nxt.val <= level;
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) {
            float t = (level - cur.val) / (nxt.val - cur.val);
            out.push_back({{
                cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                cur.pos.y + t * (nxt.pos.y - cur.pos.y)
            }, level});
        }
    }
    return out;
}

/// Compute auto levels evenly spaced across the value range.
std::vector<float> autoLevels(float vmin, float vmax, int n) {
    if (n < 2) n = 2;
    if (vmax <= vmin) vmax = vmin + 1.0f;
    std::vector<float> levels(n);
    for (int i = 0; i < n; ++i)
        levels[i] = vmin + (vmax - vmin) * i / (n - 1);
    return levels;
}

/// Find the value range of a grid.
std::pair<float, float> gridValueRange(const Grid2D& grid) {
    if (grid.valueRange.valid())
        return {grid.valueRange.min, grid.valueRange.max};
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : grid.values) {
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    return {vmin, vmax};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// ContourPlot — contour lines
// ═══════════════════════════════════════════════════════════════════════════

ContourPlot::ContourPlot(Grid2D grid, ContourConfig config)
    : grid_(std::move(grid)), config_(std::move(config)) {}

void ContourPlot::computeLevels() {
    if (!config_.levels.empty()) return;
    auto [vmin, vmax] = gridValueRange(grid_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void ContourPlot::marchingSquares() {
    segments_.clear();
    const auto& g = grid_;
    if (g.width < 2 || g.height < 2) return;

    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);

    for (uint32_t j = 0; j < g.height - 1; ++j) {
        for (uint32_t i = 0; i < g.width - 1; ++i) {
            // Corner values (BL, BR, TR, TL).
            float vBL = g.values[j * g.width + i];
            float vBR = g.values[j * g.width + (i + 1)];
            float vTR = g.values[(j + 1) * g.width + (i + 1)];
            float vTL = g.values[(j + 1) * g.width + i];
            // Corner positions.
            float x0 = g.xRange.min + i * dx;
            float x1 = x0 + dx;
            float y0 = g.yRange.min + j * dy;
            float y1 = y0 + dy;

            for (float level : config_.levels) {
                int code = ((vBL >= level) ? 1 : 0) |
                           ((vBR >= level) ? 2 : 0) |
                           ((vTR >= level) ? 4 : 0) |
                           ((vTL >= level) ? 8 : 0);
                if (code == 0 || code == 15) continue;

                const EdgePair* pairs = kMarchTable;
                if (isSaddle(code)) {
                    float center = (vBL + vBR + vTR + vTL) * 0.25f;
                    pairs = (center >= level) ? kMarchSaddleAbove : kMarchTable;
                }

                if (pairs[code].e0 >= 0) {
                    segments_.push_back(interpEdge(pairs[code].e0, level,
                                                   x0, y0, x1, y1,
                                                   vBL, vBR, vTR, vTL));
                    segments_.push_back(interpEdge(pairs[code].e1, level,
                                                   x0, y0, x1, y1,
                                                   vBL, vBR, vTR, vTL));
                }
            }
        }
    }
}

void ContourPlot::prepare(render::Renderer& r) {
    computeLevels();
    marchingSquares();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!segments_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{segments_}, config_.lineColor,
                         config_.lineWidth);
    }
    prepared_ = true;
}

void ContourPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));
}

void ContourPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}

// ═══════════════════════════════════════════════════════════════════════════
// ContourfPlot — filled contour bands
// ═══════════════════════════════════════════════════════════════════════════

ContourfPlot::ContourfPlot(Grid2D grid, ContourConfig config)
    : grid_(std::move(grid)), config_(std::move(config)) {}

Color ContourfPlot::legendColor() const {
    if (config_.cmap) return config_.cmap->sample(0.5f);
    return Color::fromRgba8(128, 128, 128, 255);
}

void ContourfPlot::computeLevels() {
    if (!config_.levels.empty()) return;
    auto [vmin, vmax] = gridValueRange(grid_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void ContourfPlot::marchingSquaresFilled() {
    positions_.clear();
    colors_.clear();
    const auto& g = grid_;
    if (g.width < 2 || g.height < 2) return;

    auto [vmin, vmax] = gridValueRange(g);
    float vrange = vmax - vmin;
    if (vrange <= 0.0f) vrange = 1.0f;

    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);

    // Build level bands: [levels[0], levels[1]], [levels[1], levels[2]], ...
    // Plus below-min and above-max bands.
    const auto& L = config_.levels;
    int numBands = static_cast<int>(L.size()) - 1;

    for (uint32_t j = 0; j < g.height - 1; ++j) {
        for (uint32_t i = 0; i < g.width - 1; ++i) {
            float vBL = g.values[j * g.width + i];
            float vBR = g.values[j * g.width + (i + 1)];
            float vTR = g.values[(j + 1) * g.width + (i + 1)];
            float vTL = g.values[(j + 1) * g.width + i];
            float x0 = g.xRange.min + i * dx;
            float x1 = x0 + dx;
            float y0 = g.yRange.min + j * dy;
            float y1 = y0 + dy;

            // Cell polygon (BL, BR, TR, TL).
            ClipVertex cell[] = {
                {{x0, y0}, vBL},
                {{x1, y0}, vBR},
                {{x1, y1}, vTR},
                {{x0, y1}, vTL},
            };

            for (int b = 0; b <= numBands; ++b) {
                float lo = (b == 0) ? (vmin - 1.0f) : L[b - 1];
                float hi = (b == numBands) ? (vmax + 1.0f) : L[b];

                // Skip bands that don't intersect this cell.
                float cellMin = std::min({vBL, vBR, vTR, vTL});
                float cellMax = std::max({vBL, vBR, vTR, vTL});
                if (cellMax < lo || cellMin > hi) continue;

                // Clip cell to [lo, hi] band.
                auto poly = clipAbove(cell, lo);
                poly = clipBelow(poly, hi);
                if (poly.size() < 3) continue;

                // Compute band color.
                float mid = (lo + hi) * 0.5f;
                float t = (mid - vmin) / vrange;
                t = std::clamp(t, 0.0f, 1.0f);
                Color color = config_.cmap ? config_.cmap->sample(t)
                                           : Color::fromRgba8(
                                                 static_cast<uint8_t>(255 * t),
                                                 static_cast<uint8_t>(255 * t),
                                                 static_cast<uint8_t>(255 * t));

                // Fan triangulate.
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    positions_.push_back(poly[0].pos);
                    positions_.push_back(poly[k].pos);
                    positions_.push_back(poly[k + 1].pos);
                    colors_.push_back(color);
                    colors_.push_back(color);
                    colors_.push_back(color);
                }
            }
        }
    }
}

void ContourfPlot::prepare(render::Renderer& r) {
    computeLevels();
    marchingSquaresFilled();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!positions_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{positions_}, std::span{colors_});
    }
    prepared_ = true;
}

void ContourfPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || positions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void ContourfPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}

} // namespace volcano::plot
