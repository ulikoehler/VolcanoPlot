// volcano/plot/plots/LinePlot.hpp — line plot layer
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
namespace volcano::plot {
class LinePlot : public IPlot {
public:
    explicit LinePlot(Series2D series) : series_(std::move(series)) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return series_.label; }
    [[nodiscard]] Color legendColor() const override { return series_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    Series2D& series() noexcept { return series_; }
private:
    Series2D series_;
    render::primitives::LineRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
