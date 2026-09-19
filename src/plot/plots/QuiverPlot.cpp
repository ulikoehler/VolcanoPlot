// volcano/plot/plots/QuiverPlot.cpp — quiver (vector field) plot implementation
#include "volcano/plot/plots/QuiverPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Convert data coordinates to pixel coordinates (scale/projection aware).
Point2D dataToPixel(const Axes& axes, float dx, float dy, const Rect2D& rect) {
    Point2D f = axes.dataToFraction({dx, dy});
    return {
        rect.x + f.x * rect.width,
        rect.y + (1.0f - f.y) * rect.height
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

    // mpl `pivot`: fraction of the arrow placed before the grid point.
    float pivot = cfg_.pivot == QuiverConfig::Pivot::Tip ? 1.0f
                : cfg_.pivot == QuiverConfig::Pivot::Middle ? 0.5f : 0.0f;

    // mpl-style head dims (multiples of shaft width) take precedence over
    // the pixel headLength/headWidth when any is set.
    float shaftW = cfg_.width > 0.0f ? cfg_.width : cfg_.lineWidth;
    bool mplHead = cfg_.headwidth > 0.0f || cfg_.headlength > 0.0f ||
                   cfg_.headaxislength > 0.0f;
    float hw2 = (cfg_.headwidth > 0.0f ? cfg_.headwidth : 3.0f) * shaftW * 0.5f;
    float hl = (cfg_.headlength > 0.0f ? cfg_.headlength : 5.0f) * shaftW;
    float hal = (cfg_.headaxislength > 0.0f ? cfg_.headaxislength : 4.5f) *
                shaftW;

    for (size_t i = 0; i < n; ++i) {
        // Arrow start and end in data space; pivot shifts the whole arrow
        // along its direction so tail/middle/tip anchors at (x, y).
        float ax = u_[i] * scale, ay = v_[i] * scale;
        float sx = x_[i] - ax * pivot, sy = y_[i] - ay * pivot;
        float ex = sx + ax, ey = sy + ay;

        // Shaft as a line segment in data space.
        shaftSegs_.push_back({sx, sy});
        shaftSegs_.push_back({ex, ey});

        // Arrowhead in pixel space, then convert back to data space.
        Point2D pStart = dataToPixel(axes, sx, sy, rect);
        Point2D pEnd = dataToPixel(axes, ex, ey, rect);
        float dx = pEnd.x - pStart.x;
        float dy = pEnd.y - pStart.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) continue;  // too short for arrowhead

        float ux = dx / len, uy = dy / len;
        // Perpendicular.
        float px = -uy, py = ux;

        // Keep arrowheads in pixel space — drawn with an identity
        // transform so non-linear scales don't warp the head shape.
        Point2D tip = pEnd;
        if (mplHead) {
            // mpl head polygon: tip, ±headwidth/2 at headlength back, and
            // a notch on the shaft axis at headaxislength back.
            Point2D base1 = {pEnd.x - ux * hl + px * hw2,
                             pEnd.y - uy * hl + py * hw2};
            Point2D base2 = {pEnd.x - ux * hl - px * hw2,
                             pEnd.y - uy * hl - py * hw2};
            Point2D axis = {pEnd.x - ux * hal, pEnd.y - uy * hal};
            headFillPos_.push_back(tip);
            headFillPos_.push_back(base1);
            headFillPos_.push_back(axis);
            headFillPos_.push_back(tip);
            headFillPos_.push_back(axis);
            headFillPos_.push_back(base2);
            for (int j = 0; j < 6; ++j) headFillColors_.push_back(cfg_.color);
        } else {
            // Arrowhead triangle: tip at pEnd, base at pEnd - headLen*u ± headW/2*p.
            float l = cfg_.headLength;
            float hw = cfg_.headWidth * 0.5f;
            Point2D base1 = {pEnd.x - ux * l + px * hw, pEnd.y - uy * l + py * hw};
            Point2D base2 = {pEnd.x - ux * l - px * hw, pEnd.y - uy * l - py * hw};
            headFillPos_.push_back(tip);
            headFillPos_.push_back(base1);
            headFillPos_.push_back(base2);
            for (int j = 0; j < 3; ++j) headFillColors_.push_back(cfg_.color);
        }
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
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Upload shaft segments (mpl `width` overrides lineWidth when set).
    float shaftW = cfg_.width > 0.0f ? cfg_.width : cfg_.lineWidth;
    if (!shaftSegs_.empty()) {
        shaftRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{shaftSegs_}, cfg_.color, shaftW);
        shaftRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(shaftSegs_.size()));
    }

    // Upload arrowhead triangles (pixel space → identity transform).
    if (cfg_.filledHeads && !headFillPos_.empty()) {
        headRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{headFillPos_}, std::span{headFillColors_});
        auto ext = r.backend().extent();
        Transform2D tpix;
        tpix.view.x = {0.0f, static_cast<float>(ext.width)};
        tpix.view.y = {static_cast<float>(ext.height), 0.0f};
        tpix.view.z = {0, 1};
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        headRenderer_.draw(cmd, fullRect, tpix);
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
