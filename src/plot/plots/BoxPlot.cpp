// volcano/plot/plots/BoxPlot.cpp
#include "volcano/plot/plots/BoxPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>

namespace volcano::plot {

float BoxPlot::percentile(const std::vector<float>& sorted, float p) {
    if (sorted.empty()) return 0.0f;
    if (sorted.size() == 1) return sorted[0];
    float rank = p / 100.0f * (sorted.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(rank));
    size_t hi = static_cast<size_t>(std::ceil(rank));
    if (lo == hi) return sorted[lo];
    float frac = rank - lo;
    return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

BoxPlot::Stats BoxPlot::computeStats(const std::vector<float>& data) const {
    Stats s{};
    if (data.empty()) return s;

    std::vector<float> sorted = data;
    std::sort(sorted.begin(), sorted.end());

    s.min = sorted.front();
    s.max = sorted.back();
    s.q1 = percentile(sorted, 25.0f);
    s.median = percentile(sorted, 50.0f);
    s.q3 = percentile(sorted, 75.0f);
    float iqr = s.q3 - s.q1;

    // Compute whisker bounds.
    if (cfg_.whisker == BoxWhiskerType::MinMax) {
        s.whiskerLo = s.min;
        s.whiskerHi = s.max;
    } else if (cfg_.whisker == BoxWhiskerType::Percentile) {
        s.whiskerLo = percentile(sorted, cfg_.whiskerLo);
        s.whiskerHi = percentile(sorted, cfg_.whiskerHi);
    } else {
        // IQR 1.5: whiskers extend to the most extreme data point within
        // 1.5×IQR of the box edges.
        float loFence = s.q1 - 1.5f * iqr;
        float hiFence = s.q3 + 1.5f * iqr;
        // Find the actual data points at the whisker ends.
        s.whiskerLo = s.q1;  // fallback
        for (float v : sorted) {
            if (v >= loFence) { s.whiskerLo = v; break; }
        }
        s.whiskerHi = s.q3;  // fallback
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            if (*it <= hiFence) { s.whiskerHi = *it; break; }
        }
    }

    // Outliers: points beyond the whiskers.
    if (cfg_.showOutliers) {
        for (float v : data) {
            if (v < s.whiskerLo || v > s.whiskerHi)
                s.outliers.push_back(v);
        }
    }

    return s;
}

void BoxPlot::buildGeometry() {
    boxFillVerts_.clear();
    boxFillColors_.clear();
    boxEdgeSegs_.clear();
    medianSegs_.clear();
    outlierPoints_.clear();
    outlierColors_.clear();
    outlierSizes_.clear();

    size_t nGroups = stats_.size();
    if (nGroups == 0) return;

    float halfWidth = cfg_.boxWidth * 0.5f;
    float capHalf = cfg_.capSize * 0.5f;

    for (size_t i = 0; i < nGroups; ++i) {
        float x = static_cast<float>(i + 1);  // x position (1-based like matplotlib)
        const auto& s = stats_[i];
        if (s.q3 <= s.q1) continue;  // degenerate

        // Box fill: 2 triangles for the Q1-Q3 rectangle.
        if (cfg_.fillBox) {
            Point2D bl{x - halfWidth, s.q1}, br{x + halfWidth, s.q1};
            Point2D tl{x - halfWidth, s.q3}, tr{x + halfWidth, s.q3};
            boxFillVerts_.insert(boxFillVerts_.end(), {bl, br, tl, br, tr, tl});
            for (int j = 0; j < 6; ++j) boxFillColors_.push_back(cfg_.boxColor);
        }

        // Box edges: left, right, top, bottom (4 line segments).
        // Left edge: (x-hw, q1) → (x-hw, q3)
        boxEdgeSegs_.push_back({x - halfWidth, s.q1});
        boxEdgeSegs_.push_back({x - halfWidth, s.q3});
        // Right edge: (x+hw, q1) → (x+hw, q3)
        boxEdgeSegs_.push_back({x + halfWidth, s.q1});
        boxEdgeSegs_.push_back({x + halfWidth, s.q3});

        // Lower whisker: (x, q1) → (x, whiskerLo)
        boxEdgeSegs_.push_back({x, s.q1});
        boxEdgeSegs_.push_back({x, s.whiskerLo});
        // Lower cap: (x-capHalf, whiskerLo) → (x+capHalf, whiskerLo)
        boxEdgeSegs_.push_back({x - capHalf, s.whiskerLo});
        boxEdgeSegs_.push_back({x + capHalf, s.whiskerLo});

        // Upper whisker: (x, q3) → (x, whiskerHi)
        boxEdgeSegs_.push_back({x, s.q3});
        boxEdgeSegs_.push_back({x, s.whiskerHi});
        // Upper cap: (x-capHalf, whiskerHi) → (x+capHalf, whiskerHi)
        boxEdgeSegs_.push_back({x - capHalf, s.whiskerHi});
        boxEdgeSegs_.push_back({x + capHalf, s.whiskerHi});

        // Median line: (x-hw, median) → (x+hw, median)
        medianSegs_.push_back({x - halfWidth, s.median});
        medianSegs_.push_back({x + halfWidth, s.median});

        // Outlier points.
        for (float o : s.outliers) {
            outlierPoints_.push_back({x, o});
            outlierColors_.push_back(cfg_.outlierColor);
            outlierSizes_.push_back(cfg_.outlierSize);
        }
    }

    boxFillCount_ = static_cast<uint32_t>(boxFillVerts_.size());
    boxEdgeCount_ = static_cast<uint32_t>(boxEdgeSegs_.size());
    medianCount_ = static_cast<uint32_t>(medianSegs_.size());
    outlierCount_ = static_cast<uint32_t>(outlierPoints_.size());
}

void BoxPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    auto device = ctx.device.handle();
    auto queue = ctx.device.graphicsQueue();
    auto pool = ctx.graphicsPool.handle();
    auto allocator = ctx.allocator.handle();
    auto renderPass = r.backend().renderPass();
    auto samples = r.backend().sampleCount();

    // Compute statistics for each group.
    stats_.clear();
    stats_.reserve(groups_.size());
    for (const auto& g : groups_)
        stats_.push_back(computeStats(g));

    // Build geometry.
    buildGeometry();

    // Initialize renderers.
    if (cfg_.fillBox && boxFillCount_ >= 3) {
        boxFillRenderer_.init(device, renderPass, samples, r.pipelineCache());
        boxFillRenderer_.upload(device, queue, pool, allocator,
            std::span{boxFillVerts_.data(), boxFillVerts_.size()},
            std::span{boxFillColors_.data(), boxFillColors_.size()});
    }

    if (boxEdgeCount_ >= 2) {
        boxEdgeRenderer_.init(device, renderPass, samples, r.pipelineCache());
        boxEdgeRenderer_.upload(device, queue, pool, allocator,
            std::span{boxEdgeSegs_.data(), boxEdgeSegs_.size()},
            cfg_.whiskerColor, cfg_.lineWidth);
    }

    if (medianCount_ >= 2) {
        medianRenderer_.init(device, renderPass, samples, r.pipelineCache());
        medianRenderer_.upload(device, queue, pool, allocator,
            std::span{medianSegs_.data(), medianSegs_.size()},
            cfg_.medianColor, cfg_.medianWidth);
    }

    if (cfg_.showOutliers && outlierCount_ > 0) {
        outlierRenderer_.init(device, renderPass, samples,
            r.descriptorPool(), r.pipelineCache());
        outlierRenderer_.upload(device, queue, pool, allocator,
            std::span{outlierPoints_.data(), outlierPoints_.size()},
            std::span{outlierColors_.data(), outlierColors_.size()},
            std::span{outlierSizes_.data(), outlierSizes_.size()});
    }

    prepared_ = true;
}

void BoxPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Draw order: box fill → box edges + whiskers → median line → outliers.
    if (cfg_.fillBox && boxFillCount_ >= 3)
        boxFillRenderer_.draw(cmd, vrect, t);

    if (boxEdgeCount_ >= 2)
        boxEdgeRenderer_.draw(cmd, vrect, t, boxEdgeCount_);

    if (medianCount_ >= 2)
        medianRenderer_.draw(cmd, vrect, t, medianCount_);

    if (cfg_.showOutliers && outlierCount_ > 0)
        outlierRenderer_.draw(cmd, vrect, t, outlierCount_);
}

void BoxPlot::contributeToAutoscale(Viewport& v) const {
    // X range: 1 to nGroups+1 (boxes at x=1,2,...,nGroups).
    // Add half a box width padding on each side.
    float halfW = cfg_.boxWidth * 0.5f;
    size_t n = groups_.size();
    if (n == 0) return;
    v.x.min = std::min(v.x.min, 1.0f - halfW);
    v.x.max = std::max(v.x.max, static_cast<float>(n) + halfW);

    // Y range: min of all whisker lows, max of all whisker highs.
    // Include outliers if shown.
    for (const auto& s : stats_) {
        v.y.min = std::min(v.y.min, s.whiskerLo);
        v.y.max = std::max(v.y.max, s.whiskerHi);
        if (cfg_.showOutliers) {
            for (float o : s.outliers) {
                v.y.min = std::min(v.y.min, o);
                v.y.max = std::max(v.y.max, o);
            }
        }
    }
}

void BoxPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    // Use the box edge segment buffer (contains all extreme points).
    if (boxEdgeCount_ > 0) {
        auto r = reducer.reduceMinMax2D(boxEdgeRenderer_.pointBuffer(),
                                        boxEdgeRenderer_.pointCount());
        if (r) {
            v.x.min = std::min(v.x.min, r->minX);
            v.x.max = std::max(v.x.max, r->maxX);
            v.y.min = std::min(v.y.min, r->minY);
            v.y.max = std::max(v.y.max, r->maxY);
            return;
        }
    }
    contributeToAutoscale(v);
}

} // namespace volcano::plot
