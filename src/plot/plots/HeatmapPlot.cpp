// volcano/plot/plots/HeatmapPlot.cpp
#include "volcano/plot/plots/HeatmapPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
namespace volcano::plot {
void HeatmapPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(),
                   r.pipelineCache(), r.descriptorPool());
    // matplotlib auto-normalizes from data when vmin/vmax aren't given.
    if (grid_.valueRange.min > grid_.valueRange.max && !grid_.values.empty()) {
        auto [lo, hi] = std::ranges::minmax(grid_.values);
        grid_.valueRange = {lo, hi};
    }
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), grid_, cmap_);
    prepared_ = true;
}
void HeatmapPlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}
void HeatmapPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
    // Scalar range feeds the colorbar's z viewport.
    Range vr = grid_.valueRange;
    if (!vr.valid() && !grid_.values.empty()) {
        auto [lo, hi] = std::ranges::minmax(grid_.values);
        vr = {lo, hi};
    }
    v.z.min = std::min(v.z.min, vr.min);
    v.z.max = std::max(v.z.max, vr.max);
}
} // namespace volcano::plot
