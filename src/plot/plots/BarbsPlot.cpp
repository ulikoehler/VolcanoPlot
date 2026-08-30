// volcano/plot/plots/BarbsPlot.cpp — wind barb plot implementation
#include "volcano/plot/plots/BarbsPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

BarbsPlot::BarbsPlot(std::vector<float> x, std::vector<float> y,
                     std::vector<float> u, std::vector<float> v,
                     BarbsConfig config)
    : x_(std::move(x)), y_(std::move(y)), u_(std::move(u)), v_(std::move(v)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != u_.size() || x_.size() != v_.size())
        throw std::invalid_argument("BarbsPlot: x, y, u, v must have the same size");
}

void BarbsPlot::buildBarbs(const Viewport& vp, const Rect2D& rect) {
    segments_.clear();

    // Compute pixel scaling: how many pixels per data unit.
    float pxPerDataX = rect.width / vp.x.span();
    float pxPerDataY = rect.height / vp.y.span();

    // Shaft length in pixels.
    float shaftPx = config_.length * std::min(pxPerDataX, pxPerDataY) / 7.0f;
    // Clamp to reasonable range.
    shaftPx = std::clamp(shaftPx, 10.0f, 40.0f);

    // Barb dimensions in pixels.
    float halfBarbLen = shaftPx * 0.18f;   // half barb (5 kt)
    float fullBarbLen = shaftPx * 0.28f;   // full barb (10 kt)
    float flagLen = shaftPx * 0.4f;        // flag (50 kt)
    float barbSpacing = shaftPx * 0.15f;   // spacing between barbs along shaft

    for (size_t i = 0; i < x_.size(); ++i) {
        float u = u_[i], v = v_[i];
        float speed = std::sqrt(u * u + v * v);
        if (speed < 0.01f) continue;  // calm — no barb

        // Convert (x, y) data to pixel coordinates.
        float nx = (x_[i] - vp.x.min) / vp.x.span();
        float ny = (y_[i] - vp.y.min) / vp.y.span();
        float px = rect.x + nx * rect.width;
        float py = rect.y + (1.0f - ny) * rect.height;

        // Wind direction: barb points FROM where wind comes.
        // In meteorological convention, the barb points in the direction
        // the wind is blowing FROM. So the shaft direction is (-u, -v).
        // In screen coords, +y is down, but v is northward (up).
        // So screen direction = (-u, +v) (flip y).
        float dirX = config_.flip ? u : -u;
        float dirY = config_.flip ? -v : v;
        float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
        if (dirLen < 1e-10f) continue;
        float ux = dirX / dirLen;
        float uy = dirY / dirLen;

        // Shaft endpoints.
        float shaftEndX = px + ux * shaftPx;
        float shaftEndY = py + uy * shaftPx;

        // Draw shaft.
        segments_.push_back({px, py});
        segments_.push_back({shaftEndX, shaftEndY});

        // Perpendicular direction for barbs (pointing to the right of shaft).
        float perpX = -uy;
        float perpY = ux;

        // Decompose speed into flags (50), full barbs (10), half barbs (5).
        // Round to nearest 5 kt.
        int speedInt = static_cast<int>(std::round(speed / 5.0f)) * 5;
        int nFlags = speedInt / 50;
        int remainder = speedInt % 50;
        int nFull = remainder / 10;
        int nHalf = (remainder % 10) / 5;

        // Position barbs along the shaft, starting from the tip.
        float pos = 1.0f;  // fraction along shaft (1 = tip, 0 = base)
        auto addBarb = [&](float barbLen) {
            float bx = px + ux * shaftPx * pos;
            float by = py + uy * shaftPx * pos;
            // Barb goes perpendicular (to the right of shaft direction).
            float ex = bx + perpX * barbLen;
            float ey = by + perpY * barbLen;
            segments_.push_back({bx, by});
            segments_.push_back({ex, ey});
            pos -= barbSpacing / shaftPx;
        };

        // Draw flags (50 kt) — as a triangle (two lines forming a pennant).
        for (int f = 0; f < nFlags; ++f) {
            float bx = px + ux * shaftPx * pos;
            float by = py + uy * shaftPx * pos;
            float ex = bx + perpX * flagLen;
            float ey = by + perpY * flagLen;
            // Flag: line from shaft to flag tip, then back to shaft at next position.
            segments_.push_back({bx, by});
            segments_.push_back({ex, ey});
            float nextPos = pos - barbSpacing / shaftPx;
            float nx2 = px + ux * shaftPx * nextPos;
            float ny2 = py + uy * shaftPx * nextPos;
            segments_.push_back({ex, ey});
            segments_.push_back({nx2, ny2});
            pos = nextPos;
        }

        // Draw full barbs (10 kt).
        for (int b = 0; b < nFull; ++b)
            addBarb(fullBarbLen);

        // Draw half barb (5 kt) — only one.
        if (nHalf > 0)
            addBarb(halfBarbLen);
    }
}

void BarbsPlot::prepare(render::Renderer& r) {
    // We need the viewport and rect to build barbs in pixel space.
    // But we don't have them at prepare() time. So we'll build in draw().
    // Just init the renderer here.
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    prepared_ = true;
}

void BarbsPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    // Build barbs in pixel space using the current viewport and rect.
    buildBarbs(axes.viewport(), rect);

    if (segments_.empty()) return;

    // Upload segments. We need to upload every frame since the segments
    // depend on the viewport. This is acceptable for barbs (small data).
    auto& ctx = r.backend().context();
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{segments_}, config_.color, config_.lineWidth);

    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    // Use identity transform (pixel space) — segments are already in pixels.
    Transform2D t;
    t.view.x = {0.0f, static_cast<float>(ext.width)};
    t.view.y = {static_cast<float>(ext.height), 0.0f};  // inverted Y (pixel space)
    t.view.z = {0, 1};
    renderer_.draw(cmd, fullRect, t, static_cast<uint32_t>(segments_.size()));
}

void BarbsPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
