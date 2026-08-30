// volcano/plot/plots/ChirpPlot.hpp — chirp signal plot with phase decomposition
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/PhaseDecomposition.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>
#include <functional>

namespace volcano::plot {

/// Configuration for ChirpPlot.
struct ChirpPlotConfig {
    /// Line color.
    Color color = Color::blue();
    /// Line width.
    float lineWidth = 1.0f;
    /// Label for legend.
    std::string label;
    /// Whether to use phase decomposition for deep-zoom precision.
    /// When true, the plot re-evaluates with phase decomposition centered
    /// at the viewport center, preserving f32 precision at deep zoom.
    bool usePhaseDecomposition = true;
};

/// Chirp signal plot — renders a linear chirp signal with f32 phase
/// decomposition for deep-zoom precision.
///
/// A linear chirp sweeps frequency from f0 to f1 over a given duration.
/// At deep zoom levels, the phase (integral of frequency) becomes very
/// large, causing f32 precision loss. This plot uses PhaseDecomposer to
/// split the phase into a large constant (computed in f64) and a small
/// delta (computed in f32), preserving full precision for the oscillation.
///
/// The plot re-evaluates when the viewport changes, centering the phase
/// decomposition at the viewport center for maximum precision.
class ChirpPlot : public IPlot {
public:
    /// Construct a linear chirp plot.
    /// f0: start frequency, f1: end frequency, duration: total time.
    ChirpPlot(double f0, double f1, double duration,
              Range xRange, uint32_t samples = 2048,
              ChirpPlotConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

    /// Re-evaluate the chirp for a new viewport (infinite zoom support).
    /// Centers the phase decomposition at the viewport center.
    void reevaluate(render::Renderer& r, Range xRange, uint32_t canvasWidth);

private:
    double f0_, f1_, duration_;
    Range xRange_;
    uint32_t samples_;
    ChirpPlotConfig config_;

    std::vector<Point2D> points_;
    render::primitives::LineRenderer renderer_;
    bool prepared_ = false;

    void evaluate();
};

} // namespace volcano::plot
