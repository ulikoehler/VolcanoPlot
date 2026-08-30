// volcano/plot/plots/StreamPlot.cpp — streamplot implementation
#include "volcano/plot/plots/StreamPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// RK4 integration step for a 2D ODE dy/dt = f(y).
template<typename F>
Point2D rk4Step(float x, float y, float h, F&& f) {
    auto [k1u, k1v] = f(x, y);
    auto [k2u, k2v] = f(x + 0.5f * h * k1u, y + 0.5f * h * k1v);
    auto [k3u, k3v] = f(x + 0.5f * h * k2u, y + 0.5f * h * k2v);
    auto [k4u, k4v] = f(x + h * k2u, y + h * k2v);
    return {
        x + h * (k1u + 2.0f * k2u + 2.0f * k3u + k4u) / 6.0f,
        y + h * (k1v + 2.0f * k2v + 2.0f * k3v + k4v) / 6.0f
    };
}

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

StreamPlot::StreamPlot(Grid2D gridU, Grid2D gridV, StreamConfig config)
    : gridU_(std::move(gridU)), gridV_(std::move(gridV)), config_(std::move(config)) {
    if (gridU_.width != gridV_.width || gridU_.height != gridV_.height)
        throw std::invalid_argument("StreamPlot: U and V grids must have the same dimensions");
    if (gridU_.width < 2 || gridU_.height < 2)
        throw std::invalid_argument("StreamPlot: grid must be at least 2x2");
}

std::pair<float, float> StreamPlot::sampleField(float x, float y) const {
    const auto& g = gridU_;
    if (x < g.xRange.min || x > g.xRange.max ||
        y < g.yRange.min || y > g.yRange.max)
        return {0.0f, 0.0f};

    // Bilinear interpolation.
    float fx = (x - g.xRange.min) / g.xRange.span() * (g.width - 1);
    float fy = (y - g.yRange.min) / g.yRange.span() * (g.height - 1);
    uint32_t i0 = static_cast<uint32_t>(fx);
    uint32_t j0 = static_cast<uint32_t>(fy);
    uint32_t i1 = std::min(i0 + 1, g.width - 1);
    uint32_t j1 = std::min(j0 + 1, g.height - 1);
    float tx = fx - i0;
    float ty = fy - j0;

    auto sample = [](const Grid2D& grid, uint32_t i, uint32_t j) -> float {
        return grid.values[j * grid.width + i];
    };

    float u00 = sample(gridU_, i0, j0), u10 = sample(gridU_, i1, j0);
    float u01 = sample(gridU_, i0, j1), u11 = sample(gridU_, i1, j1);
    float v00 = sample(gridV_, i0, j0), v10 = sample(gridV_, i1, j0);
    float v01 = sample(gridV_, i0, j1), v11 = sample(gridV_, i1, j1);

    float u = u00 * (1 - tx) * (1 - ty) + u10 * tx * (1 - ty) +
              u01 * (1 - tx) * ty + u11 * tx * ty;
    float v = v00 * (1 - tx) * (1 - ty) + v10 * tx * (1 - ty) +
              v01 * (1 - tx) * ty + v11 * tx * ty;
    return {u, v};
}

void StreamPlot::integrateStreamline(float x0, float y0, int dir,
                                     std::vector<Point2D>& points) const {
    const auto& g = gridU_;
    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);
    float cellSize = std::min(dx, dy);
    float h = config_.stepSize * cellSize * dir;

    float x = x0, y = y0;
    auto field = [this](float px, float py) -> std::pair<float, float> {
        return sampleField(px, py);
    };

    for (uint32_t step = 0; step < config_.maxPoints; ++step) {
        points.push_back({x, y});
        auto [u, v] = sampleField(x, y);
        float mag = std::sqrt(u * u + v * v);
        if (mag < 1e-10f) break;
        Point2D next = rk4Step(x, y, h, field);
        if (next.x < g.xRange.min || next.x > g.xRange.max ||
            next.y < g.yRange.min || next.y > g.yRange.max)
            break;
        x = next.x;
        y = next.y;
    }
}

bool StreamPlot::tooCloseToExisting(float x, float y, float minDist) const {
    for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
        uint32_t start = streamlineStarts_[s];
        uint32_t len = streamlineLengths_[s];
        for (uint32_t k = 0; k < len; ++k) {
            const auto& p = streamlinePoints_[start + k];
            float ddx = p.x - x, ddy = p.y - y;
            if (ddx * ddx + ddy * ddy < minDist * minDist)
                return true;
        }
    }
    return false;
}

void StreamPlot::generateStreamlines() {
    streamlinePoints_.clear();
    streamlineStarts_.clear();
    streamlineLengths_.clear();

    const auto& g = gridU_;
    float dx = g.xRange.span() / (g.width - 1);
    float dy = g.yRange.span() / (g.height - 1);
    float cellSize = std::min(dx, dy);
    float minDist = cellSize / std::max(0.1f, config_.density);

    uint32_t seedNx = std::max(2u, static_cast<uint32_t>((g.width - 1) * config_.density));
    uint32_t seedNy = std::max(2u, static_cast<uint32_t>((g.height - 1) * config_.density));

    for (uint32_t sj = 0; sj < seedNy; ++sj) {
        for (uint32_t si = 0; si < seedNx; ++si) {
            float x = g.xRange.min + (si + 0.5f) * g.xRange.span() / seedNx;
            float y = g.yRange.min + (sj + 0.5f) * g.yRange.span() / seedNy;

            if (tooCloseToExisting(x, y, minDist)) continue;

            auto [u, v] = sampleField(x, y);
            if (std::sqrt(u * u + v * v) < 1e-10f) continue;

            // Trace forward and backward.
            std::vector<Point2D> forward;
            integrateStreamline(x, y, +1, forward);
            std::vector<Point2D> backward;
            integrateStreamline(x, y, -1, backward);

            // Combine: backward (reversed, skip start) + forward.
            std::vector<Point2D> line;
            for (int k = static_cast<int>(backward.size()) - 1; k >= 1; --k)
                line.push_back(backward[k]);
            for (const auto& p : forward)
                line.push_back(p);

            if (line.size() < 2) continue;

            uint32_t startIdx = static_cast<uint32_t>(streamlinePoints_.size());
            for (const auto& p : line)
                streamlinePoints_.push_back(p);
            streamlineStarts_.push_back(startIdx);
            streamlineLengths_.push_back(static_cast<uint32_t>(line.size()));
        }
    }
}

void StreamPlot::prepare(render::Renderer& r) {
    generateStreamlines();
    auto& ctx = r.backend().context();

    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());

    // Convert streamlines to line segments (pairs for eLineList).
    std::vector<Point2D> segments;
    for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
        uint32_t start = streamlineStarts_[s];
        uint32_t len = streamlineLengths_[s];
        for (uint32_t k = 0; k + 1 < len; ++k) {
            segments.push_back(streamlinePoints_[start + k]);
            segments.push_back(streamlinePoints_[start + k + 1]);
        }
    }

    if (!segments.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{segments}, config_.color,
                             config_.lineWidth);
    }

    if (config_.arrows) {
        arrowRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                            r.backend().sampleCount(), r.pipelineCache());
    }

    prepared_ = true;
}

void StreamPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_ || streamlineStarts_.empty()) return;

    auto& ctx = r.backend().context();
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Draw streamline segments.
    uint32_t totalVerts = 0;
    for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
        uint32_t len = streamlineLengths_[s];
        if (len >= 2) totalVerts += (len - 1) * 2;  // 2 vertices per segment
    }
    if (totalVerts > 0) {
        lineRenderer_.draw(cmd, vrect, t, totalVerts);
    }

    // Draw arrowheads.
    if (config_.arrows && !streamlineStarts_.empty()) {
        arrowPositions_.clear();
        arrowColors_.clear();
        const auto& vp = axes.viewport();

        for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
            uint32_t start = streamlineStarts_[s];
            uint32_t len = streamlineLengths_[s];
            if (len < 4) continue;

            uint32_t arrowIdx = start + len / 3;
            if (arrowIdx + 1 >= start + len) continue;
            Point2D p0 = streamlinePoints_[arrowIdx];
            Point2D p1 = streamlinePoints_[arrowIdx + 1];

            Point2D pp0 = dataToPixel(p0.x, p0.y, vp, rect);
            Point2D pp1 = dataToPixel(p1.x, p1.y, vp, rect);
            float ddx = pp1.x - pp0.x, ddy = pp1.y - pp0.y;
            float plen = std::sqrt(ddx * ddx + ddy * ddy);
            if (plen < 1.0f) continue;
            float ux = ddx / plen, uy = ddy / plen;
            float px = -uy, py = ux;
            float hl = config_.arrowLength;
            float hw = config_.arrowWidth * 0.5f;

            Point2D tip = pp1;
            Point2D base1 = {pp1.x - ux * hl + px * hw, pp1.y - uy * hl + py * hw};
            Point2D base2 = {pp1.x - ux * hl - px * hw, pp1.y - uy * hl - py * hw};

            Point2D dTip = pixelToData(tip.x, tip.y, vp, rect);
            Point2D dBase1 = pixelToData(base1.x, base1.y, vp, rect);
            Point2D dBase2 = pixelToData(base2.x, base2.y, vp, rect);

            arrowPositions_.push_back(dTip);
            arrowPositions_.push_back(dBase1);
            arrowPositions_.push_back(dBase2);
            Color ac = config_.color;
            for (int k = 0; k < 3; ++k) arrowColors_.push_back(ac);
        }

        if (!arrowPositions_.empty()) {
            arrowRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                  ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                  std::span{arrowPositions_}, std::span{arrowColors_});
            arrowRenderer_.draw(cmd, vrect, t);
        }
    }
}

void StreamPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, gridU_.xRange.min);
    v.x.max = std::max(v.x.max, gridU_.xRange.max);
    v.y.min = std::min(v.y.min, gridU_.yRange.min);
    v.y.max = std::max(v.y.max, gridU_.yRange.max);
}

} // namespace volcano::plot
