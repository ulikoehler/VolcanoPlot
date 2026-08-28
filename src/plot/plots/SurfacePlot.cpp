// volcano/plot/plots/SurfacePlot.cpp
#include "volcano/plot/plots/SurfacePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
namespace volcano::plot {
void SurfacePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), grid_);
    prepared_ = true;
}
void SurfacePlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes&, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, camera_);
}
void SurfacePlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
}
} // namespace volcano::plot
