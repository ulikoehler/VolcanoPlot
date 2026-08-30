// volcano/plot/plots/StackPlot.hpp — stacked area plot (matplotlib `stackplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Stacked area plot. Multiple series are stacked on top of each other,
/// showing the cumulative contribution over time.
/// Equivalent to matplotlib's `stackplot(x, y1, y2, ...)`.
class StackPlot : public IPlot {
public:
    /// Construct from x and a 2D array of y values (one vector per series).
    StackPlot(std::vector<float> x,
              std::vector<std::vector<float>> ys,
              std::vector<Color> colors = {},
              std::vector<std::string> labels = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return overallLabel_; }
    [[nodiscard]] Color legendColor() const override;
    void setOverallLabel(std::string l) { overallLabel_ = std::move(l); }

    /// Labels for each series (for legend).
    [[nodiscard]] const std::vector<std::string>& seriesLabels() const { return labels_; }
    [[nodiscard]] const std::vector<Color>& seriesColors() const { return colors_; }

private:
    std::vector<float> x_;
    std::vector<std::vector<float>> ys_;
    std::vector<Color> colors_;
    std::vector<std::string> labels_;
    std::string overallLabel_;

    // Cumulative stacked values: stack[i][j] = sum of ys[0..i][j] + baseline.
    std::vector<std::vector<float>> stack_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeStack();
    void buildFillTriangles();
    void initDefaultColors();
};

} // namespace volcano::plot
