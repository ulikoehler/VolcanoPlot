// volcano/plot/plots/BarPlot.cpp
#include "volcano/plot/plots/BarPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
namespace volcano::plot {
void BarPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void BarPlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t; t.view = axes.viewport();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}
void BarPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, 0.0f); v.x.max = std::max(v.x.max, float(data_.heights.size()));
    v.y.min = std::min(v.y.min, 0.0f);
    for (float h : data_.heights) v.y.max = std::max(v.y.max, h);
}
} // namespace volcano::plot
