// volcano/plot/plots/ErrorbarPlot.hpp — error bar plot (matplotlib `errorbar`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <optional>
#include <string>

namespace volcano::plot {

/// Configuration for ErrorbarPlot.
struct ErrorbarConfig {
    std::vector<float> xerr;       ///< symmetric x error (±value per point)
    std::vector<float> yerr;       ///< symmetric y error (±value per point)
    std::vector<float> xerrLower;  ///< asymmetric x error (lower)
    std::vector<float> xerrUpper;  ///< asymmetric x error (upper)
    std::vector<float> yerrLower;  ///< asymmetric y error (lower)
    std::vector<float> yerrUpper;  ///< asymmetric y error (upper)
    Color color = Color::fromRgba8(31, 119, 180, 255);
    Color markerColor = Color::fromRgba8(31, 119, 180, 255);
    Color errorbarColor = Color::black();
    float markerSize = 6.0f;       ///< marker size in pixels
    float lineWidth = 1.5f;        ///< connecting line width
    float errorbarWidth = 1.0f;    ///< error bar line width
    float capSize = 3.0f;          ///< cap width in pixels (data units)
    bool drawLine = true;          ///< draw connecting line between points
    bool drawMarker = true;        ///< draw markers at each point
    bool drawCaps = true;          ///< draw caps at error bar ends
    std::string label;
};

/// Error bar plot. Renders points with optional horizontal and/or vertical
/// error bars, caps, and a connecting line. Equivalent to matplotlib's
/// `errorbar(x, y, xerr=..., yerr=...)`.
class ErrorbarPlot : public IPlot {
public:
    /// Construct from x, y arrays and configuration.
    ErrorbarPlot(std::vector<float> x, std::vector<float> y, ErrorbarConfig cfg = {})
        : x_(std::move(x)), y_(std::move(y)), cfg_(std::move(cfg)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return cfg_.label; }
    [[nodiscard]] Color legendColor() const override { return cfg_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

private:
    std::vector<float> x_, y_;
    ErrorbarConfig cfg_;

    // Renderers (initialized in prepare).
    render::primitives::LineRenderer lineRenderer_;        // connecting line
    render::primitives::LineSegmentRenderer errorRenderer_; // error bars + caps
    render::primitives::PointRenderer pointRenderer_;      // markers

    // Computed geometry (built in prepare).
    std::vector<Point2D> errorSegments_;  // line segment endpoints for error bars + caps
    uint32_t errorVertexCount_ = 0;
    bool hasErrors_ = false;
    bool prepared_ = false;

    /// Build error bar and cap line segments.
    void buildErrorSegments();

    /// Get effective x error bounds for point i.
    [[nodiscard]] std::pair<float, float> xerrBounds(size_t i) const;
    /// Get effective y error bounds for point i.
    [[nodiscard]] std::pair<float, float> yerrBounds(size_t i) const;
};

} // namespace volcano::plot
