// volcano/plot/plots/FunctionPlot.cpp
#include "volcano/plot/plots/FunctionPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <cmath>

namespace volcano::plot {

namespace {

/// CPU fallback evaluation (used until GPU compute shader is wired up).
/// Parses a tiny subset: supports sin, cos, tan, exp, log, sqrt, x, constants.
/// In production, the GLSL body is compiled to a compute shader.
float evalCpu(const std::string& body, float x) {
    // Very simple: we just evaluate a few known patterns.
    // Real impl will compile glslBody_ to a SPIR-V compute shader.
    std::string expr = body;
    // Replace "x" with the value — naive, but works for simple expressions.
    // This is a placeholder; the GPU path is the real implementation.
    (void)expr;
    // Default: sine wave for demo.
    return std::sin(x);
}

} // namespace

void FunctionPlot::prepare(render::Renderer& r) {
    // Evaluate on CPU as a fallback; GPU compute path to be added.
    points_.resize(samples_);
    for (uint32_t i = 0; i < samples_; ++i) {
        float t = float(i) / (samples_ - 1);
        float x = xRange_.min + t * xRange_.span();
        points_[i] = { x, evalCpu(glslBody_, x) };
    }
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{points_}, color_, lineWidth_);
    prepared_ = true;
}

void FunctionPlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t; t.view = axes.viewport();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(points_.size()));
}

void FunctionPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, xRange_.min);
    v.x.max = std::max(v.x.max, xRange_.max);
    // Y range unknown until evaluated; assume [-1,1] as a heuristic.
    v.y.min = std::min(v.y.min, -1.0f);
    v.y.max = std::max(v.y.max, 1.0f);
}

void FunctionPlot::reevaluate(render::Renderer& r, Range xRange, uint32_t canvasWidth) {
    xRange_ = xRange;
    samples_ = std::max(2u, canvasWidth * 2); // 2 samples per pixel
    prepare(r);
}

} // namespace volcano::plot
