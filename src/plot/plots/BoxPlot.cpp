// volcano/plot/plots/BoxPlot.cpp
#include "volcano/plot/plots/BoxPlot.hpp"
#include "volcano/plot/Path.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

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
    s.mean = std::accumulate(data.begin(), data.end(), 0.0f)
             / float(data.size());
    float iqr = s.q3 - s.q1;

    // Median CI for notched boxes (matplotlib bxp_stats).
    if (cfg_.bootstrap > 0) {
        // Bootstrap resample the median.
        std::mt19937 rng(0xC0FFEEu + uint32_t(data.size()));
        std::uniform_int_distribution<size_t> pick(0, data.size() - 1);
        std::vector<float> meds;
        meds.reserve(cfg_.bootstrap);
        for (uint32_t b = 0; b < cfg_.bootstrap; ++b) {
            std::vector<float> sample(data.size());
            for (auto& v : sample) v = data[pick(rng)];
            std::sort(sample.begin(), sample.end());
            meds.push_back(percentile(sample, 50.0f));
        }
        std::sort(meds.begin(), meds.end());
        s.notchLo = percentile(meds, 2.5f);
        s.notchHi = percentile(meds, 97.5f);
    } else {
        // Gaussian approximation: med ± 1.57·IQR/√n.
        float half = 1.57f * iqr / std::sqrt(float(data.size()));
        s.notchLo = s.median - half;
        s.notchHi = s.median + half;
    }

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

    meanSegs_.clear();

    for (size_t i = 0; i < nGroups; ++i) {
        float x = static_cast<float>(i + 1);  // x position (1-based like matplotlib)
        const auto& s = stats_[i];
        if (s.q3 <= s.q1) continue;  // degenerate

        if (cfg_.notch) {
            // Notched box polygon (matplotlib bxp notch shape).
            float nw = halfWidth * 0.5f;
            float nLo = std::max(s.notchLo, s.q1);
            float nHi = std::min(s.notchHi, s.q3);
            std::vector<Point2D> poly{
                {x - halfWidth, s.q1}, {x + halfWidth, s.q1},
                {x + halfWidth, nLo},  {x + nw, s.median},
                {x + halfWidth, nHi},  {x + halfWidth, s.q3},
                {x - halfWidth, s.q3}, {x - halfWidth, nHi},
                {x - nw, s.median},    {x - halfWidth, nLo},
            };
            if (cfg_.fillBox) {
                auto tris = earClip(poly);
                boxFillVerts_.insert(boxFillVerts_.end(),
                                     tris.begin(), tris.end());
                for (size_t j = 0; j < tris.size(); ++j)
                    boxFillColors_.push_back(cfg_.boxColor);
            }
            for (size_t k = 0; k < poly.size(); ++k) {
                boxEdgeSegs_.push_back(poly[k]);
                boxEdgeSegs_.push_back(poly[(k + 1) % poly.size()]);
            }
            // Whiskers connect at the notch waist.
            boxEdgeSegs_.push_back({x, nLo});
            boxEdgeSegs_.push_back({x, s.whiskerLo});
            boxEdgeSegs_.push_back({x - capHalf, s.whiskerLo});
            boxEdgeSegs_.push_back({x + capHalf, s.whiskerLo});
            boxEdgeSegs_.push_back({x, nHi});
            boxEdgeSegs_.push_back({x, s.whiskerHi});
            boxEdgeSegs_.push_back({x - capHalf, s.whiskerHi});
            boxEdgeSegs_.push_back({x + capHalf, s.whiskerHi});
            // Median drawn across the notch waist only.
            medianSegs_.push_back({x - nw, s.median});
            medianSegs_.push_back({x + nw, s.median});
        } else {
            // Box fill: 2 triangles for the Q1-Q3 rectangle.
            if (cfg_.fillBox) {
                Point2D bl{x - halfWidth, s.q1}, br{x + halfWidth, s.q1};
                Point2D tl{x - halfWidth, s.q3}, tr{x + halfWidth, s.q3};
                boxFillVerts_.insert(boxFillVerts_.end(), {bl, br, tl, br, tr, tl});
                for (int j = 0; j < 6; ++j) boxFillColors_.push_back(cfg_.boxColor);
            }

            // Box edges: left, right, top, bottom (4 line segments).
            boxEdgeSegs_.push_back({x - halfWidth, s.q1});
            boxEdgeSegs_.push_back({x - halfWidth, s.q3});
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
        }

        // Mean marker (matplotlib showmeans/meanline).
        if (cfg_.showMeans) {
            if (cfg_.meanLine) {
                meanSegs_.push_back({x - halfWidth, s.mean});
                meanSegs_.push_back({x + halfWidth, s.mean});
            } else {
                // Small X marker at (x, mean).
                float d = halfWidth * 0.3f;
                meanSegs_.push_back({x - d, s.mean - d});
                meanSegs_.push_back({x + d, s.mean + d});
                meanSegs_.push_back({x - d, s.mean + d});
                meanSegs_.push_back({x + d, s.mean - d});
            }
        }

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
    meanCount_ = static_cast<uint32_t>(meanSegs_.size());
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

    if (cfg_.showMeans && meanCount_ >= 2) {
        meanRenderer_.init(device, renderPass, samples, r.pipelineCache());
        meanRenderer_.upload(device, queue, pool, allocator,
            std::span{meanSegs_.data(), meanSegs_.size()},
            cfg_.meanColor, cfg_.medianWidth);
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
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Draw order: box fill → box edges + whiskers → median line → outliers.
    if (cfg_.fillBox && boxFillCount_ >= 3)
        boxFillRenderer_.draw(cmd, vrect, t);

    if (boxEdgeCount_ >= 2)
        boxEdgeRenderer_.draw(cmd, vrect, t, boxEdgeCount_);

    if (medianCount_ >= 2)
        medianRenderer_.draw(cmd, vrect, t, medianCount_);

    if (cfg_.showMeans && meanCount_ >= 2)
        meanRenderer_.draw(cmd, vrect, t, meanCount_);

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
    // Reduce over the box edge segment buffer (contains box + whisker + cap
    // vertices). Outliers are uploaded to a separate PointRenderer buffer,
    // so we reduce over both to include outlier extremes in the viewport.
    bool gotAny = false;
    if (boxEdgeCount_ > 0) {
        auto r = reducer.reduceMinMax2D(boxEdgeRenderer_.pointBuffer(),
                                        boxEdgeRenderer_.pointCount());
        if (r) {
            v.x.min = std::min(v.x.min, r->minX);
            v.x.max = std::max(v.x.max, r->maxX);
            v.y.min = std::min(v.y.min, r->minY);
            v.y.max = std::max(v.y.max, r->maxY);
            gotAny = true;
        }
    }
    if (cfg_.showOutliers && outlierCount_ > 0) {
        auto r = reducer.reduceMinMax2D(outlierRenderer_.pointBuffer(),
                                        outlierRenderer_.pointCount());
        if (r) {
            v.x.min = std::min(v.x.min, r->minX);
            v.x.max = std::max(v.x.max, r->maxX);
            v.y.min = std::min(v.y.min, r->minY);
            v.y.max = std::max(v.y.max, r->maxY);
            gotAny = true;
        }
    }
    if (!gotAny) contributeToAutoscale(v);
}

} // namespace volcano::plot
