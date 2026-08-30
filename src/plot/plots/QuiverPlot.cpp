// volcano/plot/plots/QuiverPlot.cpp — quiver (vector field) plot implementation
#include "volcano/plot/plots/QuiverPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Convert data coordinates to pixel coordinates.
Point2D dataToPixel(float dx, float dy, const Viewport& vp, const Rect2D& rect) {
    float nx = (dx - vp.x.min) / vp.x.span();
    float ny = (dy - vp.y.min) / vp.y.span();
    return {
        rect.x + nx * rect.width,
        rect.y + (1.0f - ny) * rect.height
    };
}

/// Convert pixel coordinates back to data coordinates.
Point2D pixelToData(float px, float py, const Viewport& vp, const Rect2D& rect) {
    float nx = (px - rect.x) / rect.width;
    float ny = 1.0f - (py - rect.y) / rect.height;
    return {
        vp.x.min + nx * vp.x.span(),
        vp.y.min + ny * vp.y.span()
    };
}

} // namespace

QuiverPlot::QuiverPlot(std::vector<float> x, std::vector<float> y,
                       std::vector<float> u, std::vector<float> v,
                       QuiverConfig cfg)
    : x_(std::move(x)), y_(std::move(y)), u_(std::move(u)), v_(std::move(v)),
      cfg_(std::move(cfg)) {
    if (x_.size() != y_.size() || x_.size() != u_.size() || x_.size() != v_.size())
        throw std::invalid_argument("QuiverPlot: x, y, u, v must have the same size");
}

void QuiverPlot::buildGeometry(const Axes& axes, Rect2D rect) {
    shaftSegs_.clear();
    headFillPos_.clear();
    headFillColors_.clear();

    const auto& vp = axes.viewport();
    size_t n = x_.size();
    if (n == 0) return;

    // Compute auto scale if needed.
    float scale = cfg_.scale;
    if (scale <= 0.0f) {
        // Auto: scale so that the longest arrow spans ~1/4 of the grid spacing.
        float maxMag = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float mag = std::sqrt(u_[i] * u_[i] + v_[i] * v_[i]);
            maxMag = std::max(maxMag, mag);
        }
        scale = maxMag > 0.0f ? (vp.x.span() * 0.15f / maxMag) : 1.0f;
    }

    for (size_t i = 0; i < n; ++i) {
        // Arrow start and end in data space.
        float sx = x_[i], sy = y_[i];
        float ex = sx + u_[i] * scale;
        float ey = sy + v_[i] * scale;

        // Shaft as a line segment in data space.
        shaftSegs_.push_back({sx, sy});
        shaftSegs_.push_back({ex, ey});

        // Arrowhead in pixel space, then convert back to data space.
        Point2D pStart = dataToPixel(sx, sy, vp, rect);
        Point2D pEnd = dataToPixel(ex, ey, vp, rect);
        float dx = pEnd.x - pStart.x;
        float dy = pEnd.y - pStart.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) continue;  // too short for arrowhead

        float ux = dx / len, uy = dy / len;
        // Perpendicular.
        float px = -uy, py = ux;

        // Arrowhead triangle: tip at pEnd, base at pEnd - headLen*u ± headW/2*p.
        float hl = cfg_.headLength;
        float hw = cfg_.headWidth * 0.5f;
        Point2D tip = pEnd;
        Point2D base1 = {pEnd.x - ux * hl + px * hw, pEnd.y - uy * hl + py * hw};
        Point2D base2 = {pEnd.x - ux * hl - px * hw, pEnd.y - uy * hl - py * hw};

        // Convert back to data space for FillRenderer.
        Point2D dTip = pixelToData(tip.x, tip.y, vp, rect);
        Point2D dBase1 = pixelToData(base1.x, base1.y, vp, rect);
        Point2D dBase2 = pixelToData(base2.x, base2.y, vp, rect);

        headFillPos_.push_back(dTip);
        headFillPos_.push_back(dBase1);
        headFillPos_.push_back(dBase2);
        for (int j = 0; j < 3; ++j) headFillColors_.push_back(cfg_.color);
    }
}

void QuiverPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    shaftRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                        r.backend().sampleCount(), r.pipelineCache());
    headRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    // Upload dummy data so renderers are ready. Actual geometry is built
    // in draw() since it depends on the viewport.
    Point2D dummy[] = {{0, 0}, {1, 1}};
    shaftRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                          ctx.graphicsPool.handle(), ctx.allocator.handle(),
                          std::span{dummy, 2}, cfg_.color, cfg_.lineWidth);
    Point2D dPos[] = {{0, 0}, {1, 0}, {0, 1}};
    Color dCol[] = {cfg_.color, cfg_.color, cfg_.color};
    headRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{dPos, 3}, std::span{dCol, 3});
    prepared_ = true;
}

void QuiverPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    buildGeometry(axes, rect);

    auto& ctx = r.backend().context();
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Upload shaft segments.
    if (!shaftSegs_.empty()) {
        shaftRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{shaftSegs_}, cfg_.color, cfg_.lineWidth);
        shaftRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(shaftSegs_.size()));
    }

    // Upload arrowhead triangles.
    if (cfg_.filledHeads && !headFillPos_.empty()) {
        headRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{headFillPos_}, std::span{headFillColors_});
        headRenderer_.draw(cmd, vrect, t);
    }
}

void QuiverPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
