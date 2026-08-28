// volcano/plot/plots/BarPlot.hpp
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/BarRenderer.hpp"
namespace volcano::plot {
class BarPlot : public IPlot {
public:
    explicit BarPlot(BarData data) : data_(std::move(data)) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
private:
    BarData data_;
    render::primitives::BarRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
