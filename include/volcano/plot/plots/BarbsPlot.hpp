// volcano/plot/plots/BarbsPlot.hpp — wind barb plot (matplotlib `barbs`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Barbs configuration.
struct BarbsConfig {
    /// Length of the barb shaft in data units (default: auto from spacing).
    float length = 7.0f;
    /// Whether to flip the barbs (point in the direction the wind is
    /// blowing TO, rather than FROM). Default: false (meteorological
    /// convention — barbs point to where wind comes FROM).
    bool flip = false;
    /// Line color for all barbs.
    Color color = Color::black();
    /// Line width.
    float lineWidth = 1.0f;
    /// If true, negative u values are treated as missing (NaN).
    /// (matplotlib's `barbs` uses negative values as a flag for "rounding".)
    bool rounding = false;
    /// Label for legend.
    std::string label;
};

/// Wind barb plot — renders wind speed and direction as meteorological
/// barb symbols. Equivalent to matplotlib's `barbs`.
///
/// Each barb consists of:
/// - A shaft (line from the origin in the wind direction)
/// - Flags (50 kt triangles), full barbs (10 kt), half barbs (5 kt)
///
/// The wind direction is determined by (u, v) components:
/// - u: eastward wind component
/// - v: northward wind component
/// - Wind speed = sqrt(u^2 + v^2)
/// - Barb points toward where wind comes FROM (meteorological convention)
class BarbsPlot : public IPlot {
public:
    /// Construct from (x, y, u, v) arrays.
    BarbsPlot(std::vector<float> x, std::vector<float> y,
              std::vector<float> u, std::vector<float> v,
              BarbsConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

private:
    std::vector<float> x_, y_, u_, v_;
    BarbsConfig config_;

    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> segments_;
    bool prepared_ = false;

    void buildBarbs(const Viewport& vp, const Rect2D& rect);
};

} // namespace volcano::plot
