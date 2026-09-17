// volcano/plot/plots/KDEPlot.cpp
#include "volcano/plot/plots/KDEPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>

namespace volcano::plot {

namespace {

/// Silverman's rule of thumb for bandwidth.
float silvermanBandwidth(const std::vector<Point2D>& samples, int dim) {
    if (samples.size() < 2) return 1.0f;
    double mean = 0;
    for (const auto& s : samples) mean += (dim == 0 ? s.x : s.y);
    mean /= samples.size();
    double var = 0;
    for (const auto& s : samples) {
        double d = (dim == 0 ? s.x : s.y) - mean;
        var += d * d;
    }
    var /= samples.size();
    double sigma = std::sqrt(var);
    return float(0.9 * sigma * std::pow(samples.size(), -0.2));
}

} // namespace

void KDEPlot::evaluateKdeOnGpu(render::Renderer& r) {
    grid_.width = gridW_;
    grid_.height = gridH_;

    float bwX = bandwidth_ > 0 ? bandwidth_ : silvermanBandwidth(samples_, 0);
    float bwY = bandwidth_ > 0 ? bandwidth_ : silvermanBandwidth(samples_, 1);

    // Compute data range
    Range rx{1e30f, -1e30f}, ry{1e30f, -1e30f};
    for (const auto& s : samples_) {
        rx.min = std::min(rx.min, s.x); rx.max = std::max(rx.max, s.x);
        ry.min = std::min(ry.min, s.y); ry.max = std::max(ry.max, s.y);
    }
    grid_.xRange = rx; grid_.yRange = ry;

    if (samples_.empty()) {
        grid_.values.assign(size_t(gridW_) * gridH_, 0.0f);
        return;
    }

    // GPU path: stream samples to a storage buffer and gather the
    // density per cell in a compute shader.
    auto& ctx = r.backend().context();
    if (!kdeInited_) {
        try {
            kde_.init(ctx.device.handle(), ctx.allocator.handle(),
                      ctx.device.computeQueue(), ctx.computePool.handle());
        } catch (...) {
            // No shaderc / pipeline failure — CPU fallback below.
        }
        kdeInited_ = true;
    }
    if (kde_.ready()) {
        grid_.values = kde_.eval(samples_, gridW_, gridH_,
                                 rx.min, rx.max, ry.min, ry.max, bwX, bwY);
    }

    // CPU fallback (identical math to the shader).
    if (grid_.values.empty()) {
        float inv2bwX2 = 1.0f / (2.0f * bwX * bwX);
        float inv2bwY2 = 1.0f / (2.0f * bwY * bwY);
        float norm = 1.0f / (2.0f * 3.14159265f * bwX * bwY * samples_.size());
        grid_.values.assign(size_t(gridW_) * gridH_, 0.0f);
        for (uint32_t j = 0; j < gridH_; ++j) {
            for (uint32_t i = 0; i < gridW_; ++i) {
                float gx = rx.min + (i + 0.5f) / gridW_ * rx.span();
                float gy = ry.min + (j + 0.5f) / gridH_ * ry.span();
                float sum = 0;
                for (const auto& s : samples_) {
                    float dx = gx - s.x;
                    float dy = gy - s.y;
                    sum += std::exp(-(dx * dx * inv2bwX2 + dy * dy * inv2bwY2));
                }
                grid_.values[j * gridW_ + i] = sum * norm;
            }
        }
    }

    // Compute value range
    grid_.valueRange = {1e30f, -1e30f};
    for (float v : grid_.values) {
        grid_.valueRange.min = std::min(grid_.valueRange.min, v);
        grid_.valueRange.max = std::max(grid_.valueRange.max, v);
    }
}

void KDEPlot::prepare(render::Renderer& r) {
    evaluateKdeOnGpu(r);
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(),
                   r.pipelineCache(), r.descriptorPool());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), grid_, cmap_);
    prepared_ = true;
}

void KDEPlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void KDEPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& s : samples_) {
        v.x.min = std::min(v.x.min, s.x); v.x.max = std::max(v.x.max, s.x);
        v.y.min = std::min(v.y.min, s.y); v.y.max = std::max(v.y.max, s.y);
    }
}

} // namespace volcano::plot
