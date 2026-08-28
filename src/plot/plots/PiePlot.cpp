// volcano/plot/plots/PiePlot.cpp
#include "volcano/plot/plots/PiePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
namespace volcano::plot {
void PiePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void PiePlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes&, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect);
}
} // namespace volcano::plot
