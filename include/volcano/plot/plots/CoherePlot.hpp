// volcano/plot/plots/CoherePlot.hpp — magnitude-squared coherence
// (matplotlib `cohere`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>
#include <complex>

namespace volcano::plot {

/// Cohere configuration.
struct CohereConfig {
    /// Sample rate (Hz). Determines x-axis frequency range.
    float sampleRate = 2.0f;
    /// Window function to apply before FFT.
    enum Window { Rectangular, Hann, Hamming, Blackman } window = Hann;
    /// Number of FFT points (0 = next power of 2 >= max(signal lengths)).
    uint32_t nfft = 0;
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.5f;
    /// Label for legend.
    std::string label;
};

/// Magnitude-squared coherence plot — computes and renders the coherence
/// between two 1D signals. Equivalent to matplotlib's `cohere`.
///
/// Coherence: Cxy = |Pxy|^2 / (Pxx * Pyy)
///
/// where Pxy is the cross-power spectral density and Pxx, Pyy are the
/// auto-power spectral densities. The result is in [0, 1]:
/// - 1: perfectly coherent (linear relationship at that frequency)
/// - 0: incoherent
///
/// Only the positive-frequency half is rendered (one-sided spectrum).
class CoherePlot : public IPlot {
public:
    /// Construct from two 1D signals x and y.
    CoherePlot(std::vector<float> x, std::vector<float> y,
               CohereConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

    /// Access computed frequencies (valid after prepare()).
    [[nodiscard]] const std::vector<float>& frequencies() const { return freqs_; }
    /// Access computed coherence values [0,1] (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> signalX_;
    std::vector<float> signalY_;
    CohereConfig config_;

    std::vector<float> freqs_;
    std::vector<float> values_;

    render::primitives::LineRenderer lineRenderer_;
    std::vector<Point2D> linePoints_;
    bool prepared_ = false;

    void computeCoherence();
    static void applyWindow(std::vector<std::complex<float>>& data,
                            const std::vector<float>& signal,
                            CohereConfig::Window window,
                            float& windowPower);
    static void fft(std::vector<std::complex<float>>& data);
};

} // namespace volcano::plot
