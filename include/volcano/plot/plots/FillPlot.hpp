// volcano/plot/plots/FillPlot.hpp — filled polygon plot (matplotlib `fill`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
namespace volcano::plot {

/// Filled polygon plot. Renders a closed polygon from a series of points.
/// Equivalent to matplotlib's `fill(x, y)`.
/// The polygon is closed automatically (last point connects to first).
class FillPlot : public IPlot {
public:
    explicit FillPlot(Series2D series) : series_(std::move(series)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return series_.label; }
    [[nodiscard]] Color legendColor() const override { return series_.color; }

    Series2D& series() noexcept { return series_; }

private:
    Series2D series_;
    render::primitives::FillRenderer renderer_;
    bool prepared_ = false;
};

} // namespace volcano::plot
