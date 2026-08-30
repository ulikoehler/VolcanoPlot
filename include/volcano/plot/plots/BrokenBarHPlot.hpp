// volcano/plot/plots/BrokenBarHPlot.hpp — broken horizontal bar chart
// (matplotlib `broken_barh`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// A single horizontal bar segment: (x_start, x_width) at y-range (y_start, y_height).
struct BarHSegment {
    float xStart;   ///< x-coordinate of the left edge
    float xWidth;   ///< width of the bar in x
    float yStart;   ///< y-coordinate of the bottom edge
    float yHeight;  ///< height of the bar in y
};

/// Broken barh configuration.
struct BrokenBarHConfig {
    /// Color for all bars. If alpha=0, uses per-segment colors.
    Color color = Color::fromRgba8(31, 119, 180, 200);
    /// Per-segment colors (optional, overrides `color`).
    std::vector<Color> colors;
    /// Edge color for bar borders. If alpha=0, no edges.
    Color edgeColor = Color::transparent();
    float edgeWidth = 1.0f;
    std::string label;
};

/// Broken horizontal bar chart — a collection of horizontal rectangles
/// at various y positions. Equivalent to matplotlib's `broken_barh`.
///
/// Commonly used for Gantt charts, timeline visualizations, and showing
/// discontinuous intervals at different y levels.
class BrokenBarHPlot : public IPlot {
public:
    /// Construct from segments.
    BrokenBarHPlot(std::vector<BarHSegment> segments,
                   BrokenBarHConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<BarHSegment> segments_;
    BrokenBarHConfig config_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void buildGeometry();
};

} // namespace volcano::plot
