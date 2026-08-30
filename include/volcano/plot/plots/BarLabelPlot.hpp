// volcano/plot/plots/BarLabelPlot.hpp — bar value labels (matplotlib `bar_label`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Label position relative to the bar.
enum class BarLabelPosition {
    Edge,       ///< At the edge of the bar (default, outside for positive)
    Center,     ///< At the center of the bar
};

/// Bar label configuration.
struct BarLabelConfig {
    /// Label position.
    BarLabelPosition position = BarLabelPosition::Edge;
    /// Padding from the bar edge in pixels (for Edge position).
    float padding = 3.0f;
    /// Font scale.
    float fontScale = 1.0f;
    /// Text color.
    Color color = Color::black();
    /// Format string for labels. If empty, uses "%.1g".
    /// Supported: "%d", "%.1f", "%.2f", "%.1g", "%.2g", "%.3g".
    std::string fmt;
    /// Custom labels (if empty, auto-generates from heights).
    std::vector<std::string> labels;
    /// Horizontal mode (labels placed to the right of horizontal bars).
    bool horizontal = false;
    /// Label for legend (typically empty — bar labels are annotations).
    std::string label;
};

/// Bar label plot — renders value labels on top of (or inside) bars.
/// Equivalent to matplotlib's `bar_label`.
///
/// Designed to be added as a separate layer alongside a BarPlot or
/// GroupedBarPlot. The caller provides bar positions (x centers, y bottoms,
/// heights) and the plot renders text labels at each bar.
class BarLabelPlot : public IPlot {
public:
    /// Construct from bar positions and heights (vertical bars).
    /// `x` = bar center x, `heights` = bar height, `baseline` = bar bottom y.
    BarLabelPlot(std::vector<float> x, std::vector<float> heights,
                 float baseline, BarLabelConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<float> x_, heights_;
    float baseline_;
    BarLabelConfig config_;
    std::vector<std::string> generatedLabels_;
    bool prepared_ = false;

    void generateLabels();
    std::string formatValue(float v) const;
    Point2D dataToPixel(const Viewport& v, const Rect2D& rect,
                        float dx, float dy) const;
};

} // namespace volcano::plot
