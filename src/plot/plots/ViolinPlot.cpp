// volcano/plot/plots/ViolinPlot.cpp — violin plot implementation
#include "volcano/plot/plots/ViolinPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {

/// Silverman's rule of thumb for KDE bandwidth.
float silvermanBandwidth(const std::vector<float>& data) {
    if (data.size() < 2) return 1.0f;
    float mean = 0.0f;
    for (float v : data) mean += v;
    mean /= data.size();
    float var = 0.0f;
    for (float v : data) { float d = v - mean; var += d * d; }
    var /= (data.size() - 1);
    float sigma = std::sqrt(var);
    return 0.9f * sigma * std::pow(data.size(), -0.2f);
}

/// Gaussian kernel.
float gaussianKernel(float x) {
    return std::exp(-0.5f * x * x) * 0.39894228f;  // 1/sqrt(2*pi)
}

} // namespace

std::pair<std::vector<float>, std::vector<float>>
ViolinPlot::computeKde(const std::vector<float>& data) const {
    if (data.empty()) return {};
    float bw = cfg_.bandwidth > 0.0f ? cfg_.bandwidth : silvermanBandwidth(data);
    if (bw <= 0.0f) bw = 1.0f;

    // Evaluation range: data range ± 3*bw.
    float dmin = *std::min_element(data.begin(), data.end());
    float dmax = *std::max_element(data.begin(), data.end());
    float lo = dmin - 3.0f * bw;
    float hi = dmax + 3.0f * bw;
    uint32_t n = cfg_.numPoints;
    if (n < 2) n = 2;

    std::vector<float> yEval(n), density(n);
    for (uint32_t i = 0; i < n; ++i) {
        float y = lo + (hi - lo) * i / (n - 1);
        yEval[i] = y;
        float sum = 0.0f;
        for (float d : data)
            sum += gaussianKernel((y - d) / bw);
        density[i] = sum / (data.size() * bw);
    }
    return {yEval, density};
}

ViolinPlot::Stats ViolinPlot::computeStats(const std::vector<float>& data) const {
    std::vector<float> sorted = data;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](float p) -> float {
        if (sorted.empty()) return 0.0f;
        float idx = p / 100.0f * (sorted.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = std::min(lo + 1, sorted.size() - 1);
        float frac = idx - lo;
        return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
    };
    return {pct(25.0f), pct(50.0f), pct(75.0f),
            sorted.front(), sorted.back()};
}

void ViolinPlot::buildGeometry() {
    bodyFillPos_.clear();
    bodyFillColors_.clear();
    bodyEdgeSegs_.clear();
    innerSegs_.clear();

    for (size_t g = 0; g < groups_.size(); ++g) {
        float centerX = static_cast<float>(g + 1);  // groups at x=1,2,3,...
        float halfW = cfg_.width * 0.5f;

        auto [yEval, density] = computeKde(groups_[g]);
        if (yEval.size() < 2) continue;

        // Normalize density to max=1, then scale by halfW.
        float maxD = *std::max_element(density.begin(), density.end());
        if (maxD <= 0.0f) maxD = 1.0f;

        // Build violin body as a closed polygon: right side up, left side down.
        // Right side: (centerX + w*d[i], yEval[i]) for i=0..n-1
        // Left side:  (centerX - w*d[i], yEval[i]) for i=n-1..0
        std::vector<Point2D> polygon;
        polygon.reserve(yEval.size() * 2);
        for (size_t i = 0; i < yEval.size(); ++i) {
            float w = halfW * density[i] / maxD;
            polygon.push_back({centerX + w, yEval[i]});
        }
        for (size_t i = yEval.size(); i > 0; --i) {
            float w = halfW * density[i - 1] / maxD;
            polygon.push_back({centerX - w, yEval[i - 1]});
        }

        // Fan-triangulate the polygon for fill.
        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            bodyFillPos_.push_back(polygon[0]);
            bodyFillPos_.push_back(polygon[i]);
            bodyFillPos_.push_back(polygon[i + 1]);
            for (int j = 0; j < 3; ++j) bodyFillColors_.push_back(cfg_.bodyColor);
        }

        // Edge as line segments (closed loop).
        for (size_t i = 0; i < polygon.size(); ++i) {
            bodyEdgeSegs_.push_back(polygon[i]);
            bodyEdgeSegs_.push_back(polygon[(i + 1) % polygon.size()]);
        }

        // Inner box + whisker + median.
        if (cfg_.showBox) {
            auto st = computeStats(groups_[g]);
            float boxHalfW = halfW * 0.1f;  // narrow inner box

            // Whisker (vertical line from min to max).
            innerSegs_.push_back({centerX, st.min});
            innerSegs_.push_back({centerX, st.max});

            // Box edges (vertical lines at ±boxHalfW from q1 to q3).
            innerSegs_.push_back({centerX - boxHalfW, st.q1});
            innerSegs_.push_back({centerX - boxHalfW, st.q3});
            innerSegs_.push_back({centerX + boxHalfW, st.q1});
            innerSegs_.push_back({centerX + boxHalfW, st.q3});
            // Box top and bottom (horizontal lines).
            innerSegs_.push_back({centerX - boxHalfW, st.q1});
            innerSegs_.push_back({centerX + boxHalfW, st.q1});
            innerSegs_.push_back({centerX - boxHalfW, st.q3});
            innerSegs_.push_back({centerX + boxHalfW, st.q3});

            // Median (thick horizontal line — drawn separately with medianWidth).
            // We store median segments separately by appending after innerSegs_.
            // For simplicity, we draw all inner segments with the same renderer.
            // The median line is just a horizontal line at the median y.
            innerSegs_.push_back({centerX - boxHalfW, st.median});
            innerSegs_.push_back({centerX + boxHalfW, st.median});
        }
    }
}

void ViolinPlot::prepare(render::Renderer& r) {
    buildGeometry();
    auto& ctx = r.backend().context();

    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!bodyFillPos_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{bodyFillPos_}, std::span{bodyFillColors_});
    }

    edgeRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!bodyEdgeSegs_.empty()) {
        edgeRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{bodyEdgeSegs_}, cfg_.edgeColor,
                             cfg_.lineWidth);
    }

    innerRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                        r.backend().sampleCount(), r.pipelineCache());
    if (!innerSegs_.empty()) {
        innerRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{innerSegs_}, cfg_.whiskerColor,
                              cfg_.lineWidth);
    }

    prepared_ = true;
}

void ViolinPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!bodyFillPos_.empty())
        fillRenderer_.draw(cmd, vrect, t);
    if (!bodyEdgeSegs_.empty())
        edgeRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(bodyEdgeSegs_.size()));
    if (!innerSegs_.empty())
        innerRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(innerSegs_.size()));
}

void ViolinPlot::contributeToAutoscale(Viewport& v) const {
    // X range: groups at x=1..N, with violin width.
    size_t n = groups_.size();
    if (n > 0) {
        float xMin = 1.0f - cfg_.width * 0.5f;
        float xMax = static_cast<float>(n) + cfg_.width * 0.5f;
        v.x.min = std::min(v.x.min, xMin);
        v.x.max = std::max(v.x.max, xMax);
    }
    // Y range: min/max of all data.
    for (const auto& g : groups_) {
        for (float val : g) {
            v.y.min = std::min(v.y.min, val);
            v.y.max = std::max(v.y.max, val);
        }
    }
}

} // namespace volcano::plot
