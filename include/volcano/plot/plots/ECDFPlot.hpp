// volcano/plot/plots/ECDFPlot.hpp — empirical CDF (matplotlib `ecdf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// ECDF configuration.
struct ECDFConfig {
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.5f;
    /// Fill the area under the ECDF curve.
    bool fill = false;
    /// Fill color (semi-transparent).
    Color fillColor = Color::fromRgba8(31, 119, 180, 80);
    /// Draw markers at each data point.
    bool markers = false;
    /// Marker size in pixels.
    float markerSize = 4.0f;
    /// Complementary CDF (1 - ECDF), plots the survival function.
    bool complementary = false;
    /// Label for legend.
    std::string label;
};

/// Empirical Cumulative Distribution Function plot.
/// Computes F(x) = fraction of samples <= x and renders as a step function.
/// Equivalent to matplotlib's `ecdf(x)`.
///
/// The ECDF is a monotonically increasing step function from 0 to 1.
/// At each unique data value, the function jumps by 1/n (or by the
/// multiplicity/n for repeated values).
class ECDFPlot : public IPlot {
public:
    /// Construct from raw samples.
    explicit ECDFPlot(std::vector<float> samples, ECDFConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

    /// Access computed sorted unique values (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }
    /// Access computed CDF probabilities (valid after prepare()).
    [[nodiscard]] const std::vector<float>& probabilities() const { return probs_; }

private:
    std::vector<float> samples_;
    ECDFConfig config_;

    // Computed in prepare().
    std::vector<float> values_;    // sorted unique values
    std::vector<float> probs_;     // CDF at each value

    // Step function points for rendering.
    std::vector<Point2D> stepPoints_;

    render::primitives::LineRenderer lineRenderer_;
    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeECDF();
    void buildStepPoints();
};

} // namespace volcano::plot
