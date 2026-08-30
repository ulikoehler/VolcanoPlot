// volcano/plot/plots/ECDFPlot.cpp — empirical CDF implementation
#include "volcano/plot/plots/ECDFPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>

namespace volcano::plot {

ECDFPlot::ECDFPlot(std::vector<float> samples, ECDFConfig config)
    : samples_(std::move(samples)), config_(std::move(config)) {}

void ECDFPlot::computeECDF() {
    if (samples_.empty()) {
        values_.clear();
        probs_.clear();
        return;
    }

    // Sort samples.
    std::vector<float> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    // Count unique values and their multiplicities.
    size_t n = sorted.size();
    values_.clear();
    probs_.clear();
    float cumulative = 0.0f;
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && sorted[j] == sorted[i]) ++j;
        float count = static_cast<float>(j - i);
        cumulative += count / static_cast<float>(n);
        values_.push_back(sorted[i]);
        probs_.push_back(config_.complementary ? 1.0f - cumulative : cumulative);
        i = j;
    }
}

void ECDFPlot::buildStepPoints() {
    stepPoints_.clear();
    if (values_.empty()) return;

    // ECDF is a right-continuous step function.
    // Before the first value, F(x) = 0.
    // At each value, F jumps to the new level.
    // After the last value, F(x) = 1.
    //
    // Step points (pre-style staircase):
    //   (values[0], 0) → (values[0], probs[0]) → (values[1], probs[0])
    //   → (values[1], probs[1]) → ... → (values[m], probs[m-1])
    //   → (values[m], probs[m])
    // We also extend horizontally to the edges:
    //   Start at (values[0], 0) — the function is 0 before the first value.

    float yStart = config_.complementary ? 1.0f : 0.0f;
    float yEnd = config_.complementary ? 0.0f : 1.0f;

    // Start: horizontal at yStart from -inf to values[0], then jump.
    // We render from (values[0], yStart) → (values[0], probs[0]).
    stepPoints_.push_back({values_[0], yStart});
    stepPoints_.push_back({values_[0], probs_[0]});

    // For each subsequent value: horizontal then vertical.
    for (size_t k = 1; k < values_.size(); ++k) {
        stepPoints_.push_back({values_[k], probs_[k - 1]});
        stepPoints_.push_back({values_[k], probs_[k]});
    }
}

void ECDFPlot::prepare(render::Renderer& r) {
    computeECDF();
    buildStepPoints();

    auto& ctx = r.backend().context();
    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());

    if (!stepPoints_.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{stepPoints_}, config_.color,
                             config_.lineWidth);
    }

    // Build fill triangles if requested.
    if (config_.fill && !values_.empty()) {
        fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        fillPositions_.clear();
        fillColors_.clear();

        float yStart = config_.complementary ? 1.0f : 0.0f;

        // First segment: from (values[0], yStart) to (values[0], probs[0]).
        // Rectangle: (values[0], yStart) to (values[0]+eps, probs[0]).
        // Actually, fill under the step curve. Each step is a rectangle
        // from (values[k], yStart) to (values[k+1], probs[k]).
        // But the first step starts at values[0] with height probs[0].
        // We fill from yStart to probs[k] between consecutive values.
        for (size_t k = 0; k < values_.size(); ++k) {
            float x0 = values_[k];
            float x1 = (k + 1 < values_.size()) ? values_[k + 1] : x0;
            float y0 = yStart;
            float y1 = probs_[k];
            if (x1 <= x0 || y1 == y0) continue;
            Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int j = 0; j < 6; ++j) fillColors_.push_back(config_.fillColor);
        }

        if (!fillPositions_.empty()) {
            fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                 ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                 std::span{fillPositions_}, std::span{fillColors_});
        }
    }

    prepared_ = true;
}

void ECDFPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    if (config_.fill && !fillPositions_.empty())
        fillRenderer_.draw(cmd, vrect, t);
    if (!stepPoints_.empty())
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(stepPoints_.size()));
}

void ECDFPlot::contributeToAutoscale(Viewport& v) const {
    for (float s : samples_) {
        v.x.min = std::min(v.x.min, s);
        v.x.max = std::max(v.x.max, s);
    }
    // ECDF y-range is always [0, 1].
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, 1.0f);
}

} // namespace volcano::plot
