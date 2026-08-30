// volcano/plot/plots/ScatterPlot.cpp
#include "volcano/plot/plots/ScatterPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <limits>

namespace volcano::plot {

void ScatterPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.descriptorPool(), r.pipelineCache());

    std::vector<Color> colors(series_.points.size(), series_.color);
    std::vector<float> sizes(series_.points.size(), series_.size);
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, std::span{colors}, std::span{sizes});
    prepared_ = true;
}

void ScatterPlot::draw(vk::CommandBuffer cmd, render::Renderer& /*r*/,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(series_.points.size()));
}

void ScatterPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x);
        v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y);
        v.y.max = std::max(v.y.max, p.y);
    }
}

void ScatterPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(), renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
