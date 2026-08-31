// volcano/plot/plots/PiePlot.hpp
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/PieRenderer.hpp"
namespace volcano::plot {
class PiePlot : public IPlot {
public:
    explicit PiePlot(PieData data) : data_(std::move(data)) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override { (void)v; }
    LegendMarker legendMarker() const override { return LegendMarker::Square; }
private:
    PieData data_;
    render::primitives::PieRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
