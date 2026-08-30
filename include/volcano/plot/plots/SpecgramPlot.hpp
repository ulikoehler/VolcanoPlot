// volcano/plot/plots/SpecgramPlot.hpp — spectrogram (matplotlib `specgram`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>
#include <complex>

namespace volcano::plot {

/// Specgram configuration.
struct SpecgramConfig {
    /// Sample rate (Hz).
    float sampleRate = 2.0f;
    /// Window function to apply before FFT.
    enum Window { Rectangular, Hann, Hamming, Blackman } window = Hann;
    /// FFT window size (0 = 256).
    uint32_t nfft = 256;
    /// Overlap between successive windows in samples (0 = no overlap).
    uint32_t noverlap = 128;
    /// Colormap for coloring (nullptr = viridis).
    const Colormap* cmap = nullptr;
    /// Explicit value range (dB) for color mapping. If invalid, computed from data.
    Range valueRange{0, 0};
    /// Label for legend (typically empty).
    std::string label;
};

/// Spectrogram plot — computes and renders the STFT (short-time Fourier
/// transform) of a 1D signal as a 2D colormap. Equivalent to matplotlib's
/// `specgram`.
///
/// The signal is divided into overlapping windows of size `nfft`, each
/// windowed and FFT'd. The magnitude (in dB) is displayed as a 2D image:
/// - X-axis: time (seconds)
/// - Y-axis: frequency (Hz)
/// - Color: magnitude in dB
///
/// Only the positive-frequency half is rendered (one-sided spectrum).
/// Frequency axis goes from 0 (bottom) to sampleRate/2 (top).
class SpecgramPlot : public IPlot {
public:
    /// Construct from a 1D signal.
    SpecgramPlot(std::vector<float> signal, SpecgramConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

    /// Access computed spectrogram data (nrows × ncols, row-major).
    /// nrows = nfft/2 (one-sided), ncols = number of time windows.
    [[nodiscard]] const std::vector<float>& data() const { return data_; }
    [[nodiscard]] uint32_t numRows() const { return nrows_; }
    [[nodiscard]] uint32_t numCols() const { return ncols_; }

private:
    std::vector<float> signal_;
    SpecgramConfig config_;

    // Computed spectrogram: nrows (freq) × ncols (time), row-major.
    // Row 0 = lowest frequency (DC), row nrows-1 = Nyquist.
    std::vector<float> data_;
    uint32_t nrows_ = 0;
    uint32_t ncols_ = 0;
    Range valueRange_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeSpecgram();
    void buildGeometry();
    static void fft(std::vector<std::complex<float>>& data);
};

} // namespace volcano::plot
