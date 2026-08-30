// volcano/plot/plots/ChirpPlot.cpp — chirp signal plot with phase decomposition
#include "volcano/plot/plots/ChirpPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>

namespace volcano::plot {

ChirpPlot::ChirpPlot(double f0, double f1, double duration,
                     Range xRange, uint32_t samples, ChirpPlotConfig config)
    : f0_(f0), f1_(f1), duration_(duration), xRange_(xRange),
      samples_(samples), config_(std::move(config)) {
    if (duration_ <= 0.0)
        duration_ = 1.0;  // avoid division by zero
}

void ChirpPlot::evaluate() {
    points_.clear();
    if (samples_ < 2) return;

    points_.resize(samples_);
    float xMin = xRange_.min;
    float xSpan = xRange_.span();
    float xCenter = xMin + xSpan * 0.5f;

    LinearChirp chirp{f0_, f1_, duration_};

    for (uint32_t i = 0; i < samples_; ++i) {
        float t = xMin + (float(i) / float(samples_ - 1)) * xSpan;
        float y;
        if (config_.usePhaseDecomposition) {
            y = chirp.evaluateDecomposed(t, xCenter);
        } else {
            y = chirp.evaluate(t);
        }
        points_[i] = {t, y};
    }
}

void ChirpPlot::prepare(render::Renderer& r) {
    evaluate();

    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (points_.size() >= 2) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{points_}, config_.color, config_.lineWidth);
    }
    prepared_ = true;
}

void ChirpPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_ || points_.size() < 2) return;

    Transform2D t;
    t.view = axes.viewport();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(points_.size()));
}

void ChirpPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, xRange_.min);
    v.x.max = std::max(v.x.max, xRange_.max);
    // Chirp signal is bounded by [-1, 1] (sinusoidal).
    v.y.min = std::min(v.y.min, -1.0f);
    v.y.max = std::max(v.y.max, 1.0f);
}

void ChirpPlot::reevaluate(render::Renderer& r, Range xRange, uint32_t canvasWidth) {
    xRange_ = xRange;
    samples_ = std::max(2u, canvasWidth * 2); // 2 samples per pixel
    prepare(r);
}

} // namespace volcano::plot
