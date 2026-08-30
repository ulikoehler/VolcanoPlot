// volcano/plot/plots/GroupedBarPlot.hpp — grouped bar chart
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Grouped bar chart configuration.
struct GroupedBarConfig {
    /// Width of each individual bar (in data units, relative to 1.0 per group).
    float barWidth = 0.8f;
    /// Colors for each series. If empty, uses tab10 palette.
    std::vector<Color> colors;
    /// Labels for each series (for legend).
    std::vector<std::string> seriesLabels;
    /// Labels for each group (x-axis categories).
    std::vector<std::string> groupLabels;
    /// Baseline for bars (typically 0).
    float baseline = 0.0f;
    /// Horizontal orientation (bars extend along x-axis).
    bool horizontal = false;
    /// Overall label for the plot layer.
    std::string label;
};

/// Grouped bar chart — multiple series of bars placed side-by-side at each
/// category position. Equivalent to matplotlib's grouped bar pattern.
///
/// `heights` is a 2D array: heights[series][group].
/// For `nSeries` series and `nGroups` groups, each group occupies one unit
/// on the x-axis (group i is centered at x=i+0.5).
class GroupedBarPlot : public IPlot {
public:
    /// Construct from 2D heights array.
    /// `heights` has nSeries rows, each with nGroups elements.
    GroupedBarPlot(std::vector<std::vector<float>> heights,
                   GroupedBarConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<std::vector<float>> heights_;
    GroupedBarConfig config_;
    uint32_t nSeries_ = 0;
    uint32_t nGroups_ = 0;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void buildGeometry();
    Color seriesColor(uint32_t s) const;
};

} // namespace volcano::plot
