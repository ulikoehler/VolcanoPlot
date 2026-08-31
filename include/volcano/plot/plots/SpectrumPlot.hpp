// volcano/plot/plots/SpectrumPlot.hpp — magnitude/phase/angle spectrum
// (matplotlib `magnitude_spectrum`, `phase_spectrum`, `angle_spectrum`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <vector>
#include <string>
#include <complex>

namespace volcano::plot {

/// Spectrum type.
enum class SpectrumType {
    Magnitude,  ///< |FFT(x)| — amplitude spectrum
    Phase,      ///< atan2(im, re) wrapped to [-pi, pi] — phase spectrum
    Angle,      ///< same as Phase (alias for matplotlib's angle_spectrum)
};

/// Y-axis scale for magnitude spectrum.
enum class SpectrumScale {
    Linear,  ///< linear magnitude
    dB,      ///< 20 * log10(magnitude)
};

/// Spectrum plot configuration.
struct SpectrumConfig {
    /// Type of spectrum to compute.
    SpectrumType type = SpectrumType::Magnitude;
    /// Y-axis scale (only applies to Magnitude type).
    SpectrumScale scale = SpectrumScale::Linear;
    /// Sample rate (Hz). Determines x-axis frequency range.
    float sampleRate = 2.0f;
    /// Window function to apply before FFT.
    enum Window { Rectangular, Hann, Hamming, Blackman } window = Rectangular;
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.5f;
    /// Label for legend.
    std::string label;
};

/// Spectrum plot — computes and renders the magnitude, phase, or angle
/// spectrum of a 1D signal using FFT.
///
/// - `magnitude_spectrum(x)` — |FFT(x)| vs frequency
/// - `phase_spectrum(x)` — phase of FFT(x) vs frequency
/// - `angle_spectrum(x)` — unwrapped angle of FFT(x) vs frequency
///
/// Uses a radix-2 Cooley-Tukey FFT (signal is zero-padded to next power of 2).
/// Only the positive-frequency half is rendered (one-sided spectrum).
class SpectrumPlot : public IPlot {
public:
    /// Construct from a 1D signal.
    SpectrumPlot(std::vector<float> signal, SpectrumConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

    /// Access computed frequencies (valid after prepare()).
    [[nodiscard]] const std::vector<float>& frequencies() const { return freqs_; }
    /// Access computed spectrum values (valid after prepare()).
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> signal_;
    SpectrumConfig config_;

    // Computed in prepare().
    std::vector<float> freqs_;
    std::vector<float> values_;

    render::primitives::LineRenderer lineRenderer_;
    std::vector<Point2D> linePoints_;
    bool prepared_ = false;

    void computeSpectrum();
    void applyWindow(std::vector<std::complex<float>>& data) const;
    static void fft(std::vector<std::complex<float>>& data);
};

} // namespace volcano::plot
