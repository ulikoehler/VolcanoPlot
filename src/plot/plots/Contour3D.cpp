// volcano/plot/plots/Contour3D.cpp — 3D contour and contourf implementation
#include "volcano/plot/Ticks.hpp"
#include "volcano/plot/plots/Contour3D.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace volcano::plot {

namespace {

// ─── Marching Squares (same as 2D ContourPlot) ──────────────────────────────

struct EdgePair { int e0, e1; };

constexpr EdgePair kMarchTable[16] = {
    {-1, -1}, { 3,  0}, { 0,  1}, { 3,  1},
    { 1,  2}, { 3,  2}, { 0,  2}, { 3,  2},
    { 2,  3}, { 0,  2}, { 0,  3}, { 1,  2},
    { 1,  3}, { 0,  1}, { 0,  3}, {-1, -1},
};

constexpr EdgePair kMarchSaddleAbove[16] = {
    {-1, -1}, { 3,  0}, { 0,  1}, { 3,  1},
    { 1,  2}, { 0,  1}, { 0,  2}, { 3,  2},
    { 2,  3}, { 0,  2}, { 1,  2}, { 1,  2},
    { 1,  3}, { 0,  1}, { 0,  3}, {-1, -1},
};

constexpr bool isSaddle(int code) { return code == 5 || code == 10; }

Point2D interpEdge(int edge, float level,
                   float x0, float y0, float x1, float y1,
                   float vBL, float vBR, float vTR, float vTL) {
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
    switch (edge) {
    case 0: { float t = (level - vBL) / (vBR - vBL); return {lerp(x0, x1, t), y0}; }
    case 1: { float t = (level - vBR) / (vTR - vBR); return {x1, lerp(y0, y1, t)}; }
    case 2: { float t = (level - vTR) / (vTL - vTR); return {lerp(x1, x0, t), y1}; }
    default: { float t = (level - vTL) / (vBL - vTL); return {x0, lerp(y1, y0, t)}; }
    }
}

struct ClipVertex { Point2D pos; float val; };

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
            out.push_back({{cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                            cur.pos.y + t * (nxt.pos.y - cur.pos.y)}, level});
        }
    }
    return out;
}

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
            out.push_back({{cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                            cur.pos.y + t * (nxt.pos.y - cur.pos.y)}, level});
        }
    }
    return out;
}

std::vector<float> autoLevels(float vmin, float vmax, int n) {
    if (n < 2) n = 2;
    if (vmax <= vmin) vmax = vmin + 1.0f;
    // matplotlib _autolev: MaxNLocator(N+1) nice-number levels.
    return MaxNLocator(n + 1).tickValues(vmin, vmax);
}

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
// Contour3D — 3D contour lines
// ═══════════════════════════════════════════════════════════════════════════

Contour3D::Contour3D(Grid2D grid, Contour3DConfig config)
    : grid_(std::move(grid)), config_(std::move(config)) {}

void Contour3D::computeLevels() {
    if (!config_.levels.empty()) return;
    auto [vmin, vmax] = gridValueRange(grid_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void Contour3D::marchingSquares() {
    segments_.clear();
    segLevels_.clear();
    const auto& g = grid_;
    if (g.width < 2 || g.height < 2) return;

    // Determine the z-level at which to draw contours.
    auto [vmin, vmax] = gridValueRange(g);
    float zLevel = config_.zOffset ? (vmin + config_.zLevel) : config_.zLevel;

    vp_ = camera_.viewProjection();

    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);

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
                    Point2D p0 = interpEdge(pairs[code].e0, level,
                                            x0, y0, x1, y1, vBL, vBR, vTR, vTL);
                    Point2D p1 = interpEdge(pairs[code].e1, level,
                                            x0, y0, x1, y1, vBL, vBR, vTR, vTL);
                    // Raw world-space z: zLevel, or the contour level
                    // itself (mpl offset=None semantics). The renderer
                    // projects them (vertex shader / CPU).
                    segments_.push_back({p0.x, p0.y,
                        config_.levelsAsZ ? level : zLevel});
                    segments_.push_back({p1.x, p1.y,
                        config_.levelsAsZ ? level : zLevel});
                    segLevels_.push_back(level);
                }
            }
        }
    }
}

void Contour3D::prepare(render::Renderer& r) {
    computeLevels();
    marchingSquares();
    (void)r;
    prepared_ = true;
}

void Contour3D::draw(render::Cmd& cmd, render::Renderer& r,
                     const Axes&, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;
    // mpl colors each contour level from the colormap (default
    // image.cmap = viridis) and dashes negative levels.
    auto toPx = [&](const Point3D& p) {
        Point2D n = projectPoint3D(vp_, p);
        return Point2D{rect.x + (n.x * 0.5f + 0.5f) * float(rect.width),
                       rect.y + (0.5f - n.y * 0.5f) * float(rect.height)};
    };
    Rect2D clip = clipRectVk(rect, r.gpu().extent());
    Extent2D res = r.gpu().extent();
    auto& spine = r.gpu().spine();

    std::map<float, std::vector<size_t>> byLevel;
    for (size_t i = 0; i + 1 < segments_.size(); i += 2)
        byLevel[segLevels_[i / 2]].push_back(i);

    float lMin = config_.levels.front(), lMax = config_.levels.back();
    float lRange = std::max(1e-9f, lMax - lMin);
    StrokeParams sp;
    sp.width = config_.lineWidth;
    for (const auto& [level, idx] : byLevel) {
        sp.dashes.clear();
        if (level < 0.0f)
            sp.dashes = dashPattern(LineStyle::Dashed, config_.lineWidth);
        std::vector<Point2D> tris;
        for (size_t i : idx) {
            Point2D seg[2] = {toPx(segments_[i]), toPx(segments_[i + 1])};
            auto mesh = strokePolyline(seg, sp);
            tris.insert(tris.end(), mesh.verts.begin(), mesh.verts.end());
        }
        Color color = config_.lineColor;
        if (config_.cmap) {
            float t = (level - lMin) / lRange;
            color = config_.cmap->sample(std::clamp(t, 0.0f, 1.0f));
        }
        if (!tris.empty())
            spine.drawTriangles(cmd, clip, res, tris, color);
    }
}

void Contour3D::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}

// ═══════════════════════════════════════════════════════════════════════════
// Contourf3D — 3D filled contour bands
// ═══════════════════════════════════════════════════════════════════════════

Contourf3D::Contourf3D(Grid2D grid, Contour3DConfig config)
    : grid_(std::move(grid)), config_(std::move(config)) {}

Color Contourf3D::legendColor() const {
    if (config_.cmap) return config_.cmap->sample(0.5f);
    return Color::fromRgba8(128, 128, 128, 255);
}

void Contourf3D::computeLevels() {
    if (!config_.levels.empty()) return;
    auto [vmin, vmax] = gridValueRange(grid_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void Contourf3D::marchingSquaresFilled() {
    positions_.clear();
    colors_.clear();
    const auto& g = grid_;
    if (g.width < 2 || g.height < 2) return;

    auto [vmin, vmax] = gridValueRange(g);
    float vrange = vmax - vmin;
    if (vrange <= 0.0f) vrange = 1.0f;

    float zLevel = config_.zOffset ? (vmin + config_.zLevel) : config_.zLevel;
    vpf_ = camera_.viewProjection();
    const auto& vp = vpf_;

    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);

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

            ClipVertex cell[] = {
                {{x0, y0}, vBL}, {{x1, y0}, vBR},
                {{x1, y1}, vTR}, {{x0, y1}, vTL},
            };

            for (int b = 0; b <= numBands; ++b) {
                float lo = (b == 0) ? (vmin - 1.0f) : L[b - 1];
                float hi = (b == numBands) ? (vmax + 1.0f) : L[b];

                float cellMin = std::min({vBL, vBR, vTR, vTL});
                float cellMax = std::max({vBL, vBR, vTR, vTL});
                if (cellMax < lo || cellMin > hi) continue;

                auto poly = clipAbove(cell, lo);
                poly = clipBelow(poly, hi);
                if (poly.size() < 3) continue;

                float mid = (lo + hi) * 0.5f;
                float t = std::clamp((mid - vmin) / vrange, 0.0f, 1.0f);
                Color color = config_.cmap ? config_.cmap->sample(t)
                                           : Color::fromRgba8(
                                                 static_cast<uint8_t>(255 * t),
                                                 static_cast<uint8_t>(255 * t),
                                                 static_cast<uint8_t>(255 * t));

                const float bz = config_.levelsAsZ ? mid : zLevel;
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    positions_.push_back({poly[0].pos.x, poly[0].pos.y, bz});
                    positions_.push_back({poly[k].pos.x, poly[k].pos.y, bz});
                    positions_.push_back({poly[k+1].pos.x, poly[k+1].pos.y, bz});
                    for (int c = 0; c < 3; ++c) colors_.push_back(color);
                }
            }
        }
    }
}

void Contourf3D::prepare(render::Renderer& r) {
    computeLevels();
    marchingSquaresFilled();
    if (!renderer_) renderer_ = r.gpu().createFillRenderer();
    if (!positions_.empty()) {
        renderer_->upload3D(std::span{positions_}, std::span{colors_},
                            vpf_);
    }
    prepared_ = true;
}

void Contourf3D::draw(render::Cmd& cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_ || positions_.empty()) return;
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    renderer_->draw(cmd, vrect, t);
}

void Contourf3D::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}

} // namespace volcano::plot
