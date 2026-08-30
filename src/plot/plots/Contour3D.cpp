// volcano/plot/plots/Contour3D.cpp — 3D contour and contourf implementation
#include "volcano/plot/plots/Contour3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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
    std::vector<float> levels(n);
    for (int i = 0; i < n; ++i)
        levels[i] = vmin + (vmax - vmin) * i / (n - 1);
    return levels;
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

// Project a 3D point through a 4x4 row-major matrix to 2D NDC.
Point2D project3D(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
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
    const auto& g = grid_;
    if (g.width < 2 || g.height < 2) return;

    // Determine the z-level at which to draw contours.
    auto [vmin, vmax] = gridValueRange(g);
    float zLevel = config_.zOffset ? (vmin + config_.zLevel) : config_.zLevel;

    auto vp = camera_.viewProjection();

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
                    // Project to 3D at zLevel.
                    segments_.push_back(project3D(vp, p0.x, p0.y, zLevel));
                    segments_.push_back(project3D(vp, p1.x, p1.y, zLevel));
                }
            }
        }
    }
}

void Contour3D::prepare(render::Renderer& r) {
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

void Contour3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));
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
    auto vp = camera_.viewProjection();

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

                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    positions_.push_back(project3D(vp, poly[0].pos.x, poly[0].pos.y, zLevel));
                    positions_.push_back(project3D(vp, poly[k].pos.x, poly[k].pos.y, zLevel));
                    positions_.push_back(project3D(vp, poly[k+1].pos.x, poly[k+1].pos.y, zLevel));
                    for (int c = 0; c < 3; ++c) colors_.push_back(color);
                }
            }
        }
    }
}

void Contourf3D::prepare(render::Renderer& r) {
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

void Contourf3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_ || positions_.empty()) return;
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void Contourf3D::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}

} // namespace volcano::plot
