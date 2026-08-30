// volcano/plot/plots/XCorrPlot.hpp — autocorrelation / cross-correlation
// (matplotlib `acorr`, `xcorr`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// XCorr configuration.
struct XCorrConfig {
    /// Normalize the correlation. True matches matplotlib's `normed=True`.
    bool normed = true;
    /// Maximum number of lags. If 0, uses min(len(x), len(y)) - 1.
    uint32_t maxLags = 0;
    /// Stem line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Stem line width.
    float lineWidth = 1.5f;
    /// Marker size in pixels.
    float markerSize = 6.0f;
    /// Draw markers at the top of each stem.
    bool markers = true;
    /// Label for legend.
    std::string label;
};

/// Autocorrelation / cross-correlation plot.
/// - `acorr(x)` — autocorrelation of a single signal
/// - `xcorr(x, y)` — cross-correlation between two signals
///
/// Computes the correlation at lags from -maxLags to +maxLags and renders
/// as a stem plot (vertical lines from 0 to the correlation value, with
/// optional markers at the top).
class XCorrPlot : public IPlot {
public:
    /// Construct an autocorrelation plot from a single signal.
    explicit XCorrPlot(std::vector<float> x, XCorrConfig config = {});

    /// Construct a cross-correlation plot from two signals.
    XCorrPlot(std::vector<float> x, std::vector<float> y,
              XCorrConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

    /// Access computed lags (valid after prepare()).
    [[nodiscard]] const std::vector<float>& lags() const { return lags_; }
    /// Access computed correlation values (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> x_, y_;
    bool isAuto_ = false;
    XCorrConfig config_;

    // Computed in prepare().
    std::vector<float> lags_;
    std::vector<float> values_;

    render::primitives::LineSegmentRenderer stemRenderer_;
    render::primitives::PointRenderer markerRenderer_;
    std::vector<Point2D> stemSegments_;  // pairs of points for eLineList
    std::vector<Point2D> markerPoints_;
    std::vector<Color> markerColors_;
    std::vector<float> markerSizes_;
    bool prepared_ = false;

    void computeCorrelation();
};

} // namespace volcano::plot
