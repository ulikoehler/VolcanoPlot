// volcano/plot/plots/StreamPlot.cpp — streamplot implementation
#include "volcano/plot/plots/StreamPlot.hpp"
#include "volcano/render/Offload.hpp"
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

/// Convert data coordinates to pixel coordinates (scale/projection aware).
Point2D dataToPixel(const Axes& axes, float dx, float dy, const Rect2D& rect) {
    Point2D f = axes.dataToFraction({dx, dy});
    return {
        rect.x + f.x * rect.width,
        rect.y + (1.0f - f.y) * rect.height
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

    // mpl `broken_streamlines=False`: coast through invalid (zero/NaN)
    // field regions with the last strong velocity instead of ending.
    // Velocity ramps to zero as a hole is approached, so we keep the
    // last sample at >=50% of the running max magnitude.
    float lastU = 0.0f, lastV = 0.0f, maxMag = 0.0f;

    for (uint32_t step = 0; step < config_.maxPoints; ++step) {
        points.push_back({x, y});
        auto [u, v] = sampleField(x, y);
        float mag = std::sqrt(u * u + v * v);
        if (!std::isfinite(mag) || mag < 1e-10f) {
            if (config_.brokenStreamlines ||
                (lastU == 0.0f && lastV == 0.0f))
                break;
            x += h * lastU;
            y += h * lastV;
            if (x < g.xRange.min || x > g.xRange.max ||
                y < g.yRange.min || y > g.yRange.max)
                break;
            continue;
        }
        maxMag = std::max(maxMag, mag);
        if (mag >= 0.5f * maxMag) {
            lastU = u;
            lastV = v;
        }
        Point2D next = rk4Step(x, y, h, field);
        if (!std::isfinite(next.x) || !std::isfinite(next.y)) {
            // An intermediate sample hit an invalid region — same rule:
            // break when brokenStreamlines, otherwise coast through.
            if (config_.brokenStreamlines ||
                (lastU == 0.0f && lastV == 0.0f))
                break;
            next = {x + h * lastU, y + h * lastV};
        }
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

/// Candidate seed positions in mpl's scan order — the GPU traces these
/// in parallel; the CPU replays the ordered accept/reject over them.
void StreamPlot::buildSeeds() {
    seeds_.clear();
    const auto& g = gridU_;
    uint32_t seedNx = std::max(2u, static_cast<uint32_t>(
        (g.width - 1) * config_.density));
    uint32_t seedNy = std::max(2u, static_cast<uint32_t>(
        (g.height - 1) * config_.density));
    seeds_.reserve(size_t(seedNx) * seedNy * 2);
    for (uint32_t sj = 0; sj < seedNy; ++sj)
        for (uint32_t si = 0; si < seedNx; ++si) {
            seeds_.push_back(g.xRange.min +
                             (si + 0.5f) * g.xRange.span() / seedNx);
            seeds_.push_back(g.yRange.min +
                             (sj + 0.5f) * g.yRange.span() / seedNy);
        }
}

/// Append one accepted line to the concatenated store.
void StreamPlot::appendLine(const std::vector<Point2D>& line) {
    if (line.size() < 2) return;
    const uint32_t startIdx = static_cast<uint32_t>(streamlinePoints_.size());
    for (const auto& p : line) streamlinePoints_.push_back(p);
    streamlineStarts_.push_back(startIdx);
    streamlineLengths_.push_back(static_cast<uint32_t>(line.size()));
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

    if (seeds_.empty()) buildSeeds();

    for (size_t s = 0; s + 1 < seeds_.size(); s += 2) {
        float x = seeds_[s], y = seeds_[s + 1];

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

        appendLine(line);
    }
}

/// Poll the device traces and, once delivered, rebuild the streamline
/// set from them. The accept/reject order is unchanged, so the result
/// matches the CPU path.
bool StreamPlot::adoptGpuTraces(render::Renderer& r) {
    if (!gpuTrace_ || gpuTraced_) return false;
    const auto& g = gridU_;
    std::vector<float> pts;
    std::vector<uint32_t> cnt;
    if (!r.gpu().streamlines(g.values, gridV_.values, g.width, g.height,
                             g.xRange.min, g.xRange.span(),
                             g.yRange.min, g.yRange.span(), seeds_,
                             config_.stepSize, config_.maxPoints,
                             config_.brokenStreamlines, pts, cnt))
        return false;

    const size_t nSeeds = seeds_.size() / 2;
    if (cnt.size() < nSeeds * 2) return false;
    const size_t slotPts = size_t(config_.maxPoints);
    if (pts.size() < nSeeds * 2 * slotPts * 2) return false;

    const float dx = g.xRange.span() / (g.width - 1);
    const float dy = g.yRange.span() / (g.height - 1);
    const float minDist = std::min(dx, dy) / std::max(0.1f, config_.density);

    streamlinePoints_.clear();
    streamlineStarts_.clear();
    streamlineLengths_.clear();

    for (size_t s = 0; s < nSeeds; ++s) {
        const float x = seeds_[s * 2], y = seeds_[s * 2 + 1];
        if (tooCloseToExisting(x, y, minDist)) continue;
        auto [u, v] = sampleField(x, y);
        if (std::sqrt(u * u + v * v) < 1e-10f) continue;

        const uint32_t nBack = std::min(cnt[s * 2], config_.maxPoints);
        const uint32_t nFwd = std::min(cnt[s * 2 + 1], config_.maxPoints);
        const float* back = pts.data() + (s * 2 * slotPts) * 2;
        const float* fwd = back + slotPts * 2;

        std::vector<Point2D> line;
        line.reserve(size_t(nBack) + nFwd);
        for (int k = static_cast<int>(nBack) - 1; k >= 1; --k)
            line.push_back({back[k * 2], back[k * 2 + 1]});
        for (uint32_t k = 0; k < nFwd; ++k)
            line.push_back({fwd[k * 2], fwd[k * 2 + 1]});
        appendLine(line);
    }

    gpuTraced_ = true;
    return true;
}

/// Convert the streamline set to line segments (pairs for eLineList)
/// and upload them.
void StreamPlot::uploadSegments(render::Renderer& r) {
    std::vector<Point2D> segments;
    for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
        const uint32_t start = streamlineStarts_[s];
        const uint32_t len = streamlineLengths_[s];
        for (uint32_t k = 0; k + 1 < len; ++k) {
            segments.push_back(streamlinePoints_[start + k]);
            segments.push_back(streamlinePoints_[start + k + 1]);
        }
    }
    if (!segments.empty()) {
        lineRenderer_->upload(std::span{segments}, config_.color,
                              config_.lineWidth);
    }
}

void StreamPlot::prepare(render::Renderer& r) {
    buildSeeds();
    gpuTrace_ = r.gpu().supportsStreamlines() &&
                render::OffloadConfig::allowGpu(
                    render::OffloadConfig::global().streamlines);
    generateStreamlines();
    if (gpuTrace_) adoptGpuTraces(r);   // no-op until the trace lands

    if (!lineRenderer_) lineRenderer_ = r.gpu().createLineSegmentRenderer();
    uploadSegments(r);

    if (config_.arrows) {
        if (!arrowRenderer_) arrowRenderer_ = r.gpu().createFillRenderer();
    }

    prepared_ = true;
}

void StreamPlot::draw(render::Cmd& cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    // Swap in the device traces once the mailbox delivers them (the CPU
    // lines drawn until then are identical, so nothing flickers).
    if (gpuTrace_ && !gpuTraced_ && adoptGpuTraces(r)) uploadSegments(r);
    if (streamlineStarts_.empty()) return;

    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    // Draw streamline segments.
    uint32_t totalVerts = 0;
    for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
        uint32_t len = streamlineLengths_[s];
        if (len >= 2) totalVerts += (len - 1) * 2;  // 2 vertices per segment
    }
    if (totalVerts > 0) {
        lineRenderer_->draw(cmd, vrect, t, totalVerts);
    }

    // Draw arrowheads.
    if (config_.arrows && !streamlineStarts_.empty()) {
        arrowPositions_.clear();
        arrowColors_.clear();

        for (uint32_t s = 0; s < streamlineStarts_.size(); ++s) {
            uint32_t start = streamlineStarts_[s];
            uint32_t len = streamlineLengths_[s];
            if (len < 4) continue;

            uint32_t arrowIdx = start + len / 3;
            if (arrowIdx + 1 >= start + len) continue;
            Point2D p0 = streamlinePoints_[arrowIdx];
            Point2D p1 = streamlinePoints_[arrowIdx + 1];

            Point2D pp0 = dataToPixel(axes, p0.x, p0.y, rect);
            Point2D pp1 = dataToPixel(axes, p1.x, p1.y, rect);
            float ddx = pp1.x - pp0.x, ddy = pp1.y - pp0.y;
            float plen = std::sqrt(ddx * ddx + ddy * ddy);
            if (plen < 1.0f) continue;
            float ux = ddx / plen, uy = ddy / plen;
            float px = -uy, py = ux;
            float hl = config_.arrowLength * config_.arrowsize;
            float hw = config_.arrowWidth * config_.arrowsize * 0.5f;

            Point2D tip = pp1;
            Point2D base1 = {pp1.x - ux * hl + px * hw, pp1.y - uy * hl + py * hw};
            Point2D base2 = {pp1.x - ux * hl - px * hw, pp1.y - uy * hl - py * hw};

            // Arrowheads stay in pixel space (identity transform) so
            // non-linear scales don't warp the head shape.
            arrowPositions_.push_back(tip);
            arrowPositions_.push_back(base1);
            arrowPositions_.push_back(base2);
            Color ac = config_.color;
            for (int k = 0; k < 3; ++k) arrowColors_.push_back(ac);
        }

        if (!arrowPositions_.empty()) {
            arrowRenderer_->upload(std::span{arrowPositions_}, std::span{arrowColors_});
            auto ext = r.gpu().extent();
            Transform2D tpix;
            tpix.view.x = {0.0f, static_cast<float>(ext.width)};
            tpix.view.y = {static_cast<float>(ext.height), 0.0f};
            tpix.view.z = {0, 1};
            Rect2D fullRect{0, 0, ext.width, ext.height};
            arrowRenderer_->draw(cmd, fullRect, tpix);
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
