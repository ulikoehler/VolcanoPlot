// volcano/plot/plots/LinePlot.cpp
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
namespace volcano::plot {
void LinePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{series_.points}, series_.color, series_.lineWidth);
    prepared_ = true;
}
void LinePlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t; t.view = axes.viewport(); t.logX = axes.logX(); t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(series_.points.size()));
}
void LinePlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}
} // namespace volcano::plot
