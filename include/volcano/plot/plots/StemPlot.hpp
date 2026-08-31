// volcano/plot/plots/StemPlot.hpp — stem plot (matplotlib `stem`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Stem plot configuration.
struct StemConfig {
    /// Stem line color.
    Color lineColor = Color::fromRgba8(31, 119, 180, 255);
    /// Stem line width.
    float lineWidth = 1.5f;
    /// Marker color.
    Color markerColor = Color::fromRgba8(31, 119, 180, 255);
    /// Marker size in pixels.
    float markerSize = 6.0f;
    /// Marker type: "circle", "square", "diamond", "plus", "x", "star", "triangle".
    std::string markerStyle = "circle";
    /// Baseline value (y-coordinate where stems start).
    float baseline = 0.0f;
    /// Baseline color.
    Color baselineColor = Color::fromRgba8(31, 119, 180, 128);
    /// Baseline line width.
    float baselineWidth = 1.0f;
    /// Draw markers at the top of each stem.
    bool markers = true;
    /// Draw the baseline horizontal line.
    bool showBaseline = true;
    /// Label for legend.
    std::string label;
};

/// Stem plot — draws vertical lines from a baseline to each data point,
/// with optional markers at the top. Equivalent to matplotlib's `stem`.
///
/// Commonly used for visualizing discrete sequences, impulse responses,
/// and sampled signals.
class StemPlot : public IPlot {
public:
    /// Construct from (x, y) data points.
    StemPlot(std::vector<float> x, std::vector<float> y,
             StemConfig config = {});

    /// Construct from y values only (x = 0, 1, 2, ...).
    explicit StemPlot(std::vector<float> y, StemConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.markerColor; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

private:
    std::vector<float> x_, y_;
    StemConfig config_;

    render::primitives::LineSegmentRenderer stemRenderer_;
    render::primitives::LineSegmentRenderer baselineRenderer_;
    render::primitives::PointRenderer markerRenderer_;

    std::vector<Point2D> stemSegments_;  // pairs for eLineList
    std::vector<Point2D> baselineSegments_;
    std::vector<Point2D> markerPoints_;
    std::vector<Color> markerColors_;
    std::vector<float> markerSizes_;
    bool prepared_ = false;

    void buildGeometry();
};

} // namespace volcano::plot
