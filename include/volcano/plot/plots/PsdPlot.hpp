// volcano/plot/plots/PsdPlot.hpp — power spectral density
// (matplotlib `psd`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>
#include <complex>

namespace volcano::plot {

/// PSD configuration.
struct PsdConfig {
    /// Sample rate (Hz). Determines x-axis frequency range.
    float sampleRate = 2.0f;
    /// Window function to apply before FFT.
    enum Window { Rectangular, Hann, Hamming, Blackman } window = Hann;
    /// Number of FFT points (0 = next power of 2 >= signal length).
    uint32_t nfft = 0;
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.5f;
    /// Label for legend.
    std::string label;
};

/// Power spectral density plot — computes and renders the PSD of a 1D
/// signal. Equivalent to matplotlib's `psd`.
///
/// PSD = 10 * log10(|FFT(x)|^2 / (sampleRate * window_power))
///
/// The result is plotted in dB vs frequency. Only the positive-frequency
/// half is rendered (one-sided spectrum), with the DC and Nyquist components
/// scaled by 0.5 (since they are not duplicated in the full spectrum).
class PsdPlot : public IPlot {
public:
    /// Construct from a 1D signal.
    PsdPlot(std::vector<float> signal, PsdConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

    /// Access computed frequencies (valid after prepare()).
    [[nodiscard]] const std::vector<float>& frequencies() const { return freqs_; }
    /// Access computed PSD values in dB (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> signal_;
    PsdConfig config_;

    std::vector<float> freqs_;
    std::vector<float> values_;

    render::primitives::LineRenderer lineRenderer_;
    std::vector<Point2D> linePoints_;
    bool prepared_ = false;

    void computePsd();
    void applyWindow(std::vector<std::complex<float>>& data,
                     float& windowPower) const;
    static void fft(std::vector<std::complex<float>>& data);
};

} // namespace volcano::plot
