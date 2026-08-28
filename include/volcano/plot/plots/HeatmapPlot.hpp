// volcano/plot/plots/HeatmapPlot.hpp
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/HeatmapRenderer.hpp"
namespace volcano::plot {
class HeatmapPlot : public IPlot {
public:
    HeatmapPlot(Grid2D grid, const Colormap& cmap = colormaps::viridis())
        : grid_(std::move(grid)), cmap_(cmap) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
private:
    Grid2D grid_;
    const Colormap& cmap_;
    render::primitives::HeatmapRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
