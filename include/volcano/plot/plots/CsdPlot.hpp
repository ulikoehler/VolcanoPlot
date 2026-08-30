// volcano/plot/plots/CsdPlot.hpp — cross-spectral density
// (matplotlib `csd`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>
#include <complex>

namespace volcano::plot {

/// CSD configuration.
struct CsdConfig {
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

/// Cross-spectral density plot — computes and renders the CSD between two
/// 1D signals. Equivalent to matplotlib's `csd`.
///
/// CSD = 10 * log10(|FFT(x) * conj(FFT(y))| / (sampleRate * windowPower))
///
/// The result is plotted in dB vs frequency. Only the positive-frequency
/// half is rendered (one-sided spectrum), with the DC and Nyquist components
/// scaled by 0.5 (since they are not duplicated in the full spectrum).
class CsdPlot : public IPlot {
public:
    /// Construct from two 1D signals x and y.
    CsdPlot(std::vector<float> x, std::vector<float> y, CsdConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

    /// Access computed frequencies (valid after prepare()).
    [[nodiscard]] const std::vector<float>& frequencies() const { return freqs_; }
    /// Access computed CSD values in dB (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> signalX_;
    std::vector<float> signalY_;
    CsdConfig config_;

    std::vector<float> freqs_;
    std::vector<float> values_;

    render::primitives::LineRenderer lineRenderer_;
    std::vector<Point2D> linePoints_;
    bool prepared_ = false;

    void computeCsd();
    static void applyWindow(std::vector<std::complex<float>>& data,
                            const std::vector<float>& signal,
                            CsdConfig::Window window,
                            float& windowPower);
    static void fft(std::vector<std::complex<float>>& data);
};

} // namespace volcano::plot
