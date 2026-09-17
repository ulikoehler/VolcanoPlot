// volcano/plot/plots/FunctionPlot.cpp
#include "volcano/plot/plots/FunctionPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <cmath>

namespace volcano::plot {

namespace {

/// CPU fallback evaluation — used only when the GPU compute path fails
/// to compile (no shaderc, invalid GLSL body). Evaluates a tiny fixed
/// set of named bodies so callers still get a plausible curve.
float evalCpu(const std::string& body, float x) {
    if (body.find("cos") != std::string::npos) return std::cos(x);
    if (body.find("x*x") != std::string::npos) return x * x;
    if (body.find("exp") != std::string::npos) return std::exp(x);
    return std::sin(x);
}

} // namespace

void FunctionPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    if (!prepared_) {
        renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
        prepared_ = true;
    }

    // Lazy init of the compute evaluator + compile the GLSL body.
    if (!evalInited_) {
        eval_.init(ctx.device.handle(), ctx.allocator.handle(),
                   ctx.device.computeQueue(), ctx.computePool.handle());
        gpuPath_ = eval_.ready() && eval_.compile(glslBody_);
        evalInited_ = true;
    }

    // Infinite zoom: resample when the user-set viewport x-range moved.
    // Only manual viewports trigger re-evaluation — autoscale-derived
    // viewports keep the home range (avoids a padding-growth loop).
    Range want = (axes_ && axes_->manualX()) ? axes_->viewport().x
                                           : xRange_;
    auto ext = r.backend().extent();
    uint32_t wantSamples = std::max(samples_, ext.width * 2);
    if (want.min != evalRange_.min || want.max != evalRange_.max ||
        wantSamples != evalSamples_) {
        reevaluate(r, want, ext.width);
    }
}

void FunctionPlot::reevaluate(render::Renderer& r, Range xRange,
                              uint32_t canvasWidth) {
    xRange_ = xRange;
    evalRange_ = xRange;
    evalSamples_ = std::max(samples_, canvasWidth * 2); // 2 samples per px

    if (gpuPath_) {
        if (evalCap_ < evalSamples_) {
            evalBuf_ = eval_.makeOutput(evalSamples_);
            evalCap_ = evalSamples_;
        }
        eval_.eval(evalBuf_.handle(), xRange.min, xRange.max, evalSamples_);
        renderer_.bindExternalBuffer(evalBuf_.handle(), evalSamples_);
        return;
    }

    // CPU fallback (compile failed / no shaderc).
    auto& ctx = r.backend().context();
    std::vector<Point2D> points(evalSamples_);
    for (uint32_t i = 0; i < evalSamples_; ++i) {
        float t = float(i) / (evalSamples_ - 1);
        float x = xRange.min + t * xRange.span();
        points[i] = {x, evalCpu(glslBody_, x)};
    }
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{points}, color_, lineWidth_);
}

void FunctionPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    axes_ = &axes;  // bind for viewport-change detection in prepare()
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, renderer_.pointCount());
}

void FunctionPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, xRange_.min);
    v.x.max = std::max(v.x.max, xRange_.max);
    // Y range unknown until evaluated; assume [-1,1] as a heuristic.
    v.y.min = std::min(v.y.min, -1.0f);
    v.y.max = std::max(v.y.max, 1.0f);
}

void FunctionPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto res = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                      renderer_.pointCount());
    if (!res) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, res->minX);
    v.x.max = std::max(v.x.max, res->maxX);
    v.y.min = std::min(v.y.min, res->minY);
    v.y.max = std::max(v.y.max, res->maxY);
}

} // namespace volcano::plot
