// volcano/plot/plots/SpectrumPlot.cpp — magnitude/phase/angle spectrum implementation
#include "volcano/plot/plots/SpectrumPlot.hpp"
#include "volcano/plot/Mlab.hpp"
#include "volcano/render/Offload.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Next power of 2 >= n.
uint32_t nextPow2(uint32_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1; n |= n >> 2; n |= n >> 4;
    n |= n >> 8; n |= n >> 16;
    return n + 1;
}

/// Bit-reversal of an integer with given number of bits.
uint32_t bitReverse(uint32_t x, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

} // namespace

SpectrumPlot::SpectrumPlot(std::vector<float> signal, SpectrumConfig config)
    : signal_(std::move(signal)), config_(std::move(config)) {}

void SpectrumPlot::fft(std::vector<std::complex<float>>& data) {
    uint32_t n = static_cast<uint32_t>(data.size());
    if (n <= 1) return;

    // Bit-reversal permutation.
    int bits = 0;
    for (uint32_t tmp = n; tmp > 1; tmp >>= 1) ++bits;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = bitReverse(i, bits);
        if (j > i) std::swap(data[i], data[j]);
    }

    // Cooley-Tukey butterfly.
    for (uint32_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (uint32_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (uint32_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = data[i + k];
                std::complex<float> t = w * data[i + k + len / 2];
                data[i + k] = u + t;
                data[i + k + len / 2] = u - t;
                w *= wlen;
            }
        }
    }
}

/// The analysis window, sampled at `n` points. Indices past the signal
/// are zero, so zero padding contributes nothing.
std::vector<float> SpectrumPlot::windowArray(uint32_t n) const {
    uint32_t sigLen = static_cast<uint32_t>(signal_.size());
    std::vector<float> w(n, 0.0f);
    for (uint32_t i = 0; i < n && i < sigLen; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sigLen - 1);
        switch (config_.window) {
            case SpectrumConfig::Rectangular:
                w[i] = 1.0f;
                break;
            case SpectrumConfig::Hann:
                w[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                break;
            case SpectrumConfig::Hamming:
                w[i] = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                break;
            case SpectrumConfig::Blackman:
                w[i] = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                    + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                break;
        }
    }
    return w;
}

void SpectrumPlot::applyWindow(std::vector<std::complex<float>>& data) const {
    uint32_t n = static_cast<uint32_t>(data.size());
    uint32_t sigLen = static_cast<uint32_t>(signal_.size());
    const auto w = windowArray(n);

    // Trend removal over the real samples (mpl `detrend`), then the
    // window — the zero padding past the signal stays zero.
    std::vector<float> seg(n, 0.0f);
    for (uint32_t i = 0; i < sigLen && i < n; ++i) seg[i] = signal_[i];
    mlab::detrendInPlace(std::span<float>(seg.data(), std::min(sigLen, n)),
                         config_.detrend);

    // Copy signal into complex array and apply window.
    for (uint32_t i = 0; i < n; ++i)
        data[i] = std::complex<float>(seg[i] * w[i], 0.0f);
}

void SpectrumPlot::computeSpectrum(render::Renderer* r) {
    freqs_.clear();
    values_.clear();

    if (signal_.empty()) return;

    // Zero-pad to next power of 2.
    uint32_t n = nextPow2(static_cast<uint32_t>(signal_.size()));
    if (n < 2) n = 2;

    std::vector<std::complex<float>> data(n);
    // The window is an O(n) host pass; the transform itself can run on
    // the device when the `fft` offload switch allows it.
    const std::vector<float> win = windowArray(n);
    bool haveGpu = false;
    std::vector<float> hostSegs, unitWin;
    std::span<const float> sig = signal_;
    std::span<const float> winArg = win;
    mlab::Detrend devDetrend = config_.detrend;
    if (r && render::OffloadConfig::allowGpu(
                 render::OffloadConfig::global().fft)) {
        // One full-length segment; the trend is removed by the FFT
        // kernel on the device, or here when the device will not.
        if (devDetrend != mlab::Detrend::None &&
            !render::OffloadConfig::allowGpu(
                render::OffloadConfig::global().detrend)) {
            hostSegs = mlab::prepareSegments(signal_, win, n, 0, 1,
                                             devDetrend);
            unitWin.assign(n, 1.0f);
            sig = hostSegs; winArg = unitWin;
            devDetrend = mlab::Detrend::None;
        }
        if (auto spec = r->gpu().fftSegments(sig, winArg, n, 0, 1,
                                             devDetrend);
            spec && spec->size() == size_t(n) * 2) {
            for (uint32_t i = 0; i < n; ++i)
                data[i] = std::complex<float>((*spec)[i * 2],
                                              (*spec)[i * 2 + 1]);
            haveGpu = true;
        }
    }
    if (!haveGpu) {
        applyWindow(data);
        fft(data);
    }

    // One-sided spectrum: frequencies [0, sampleRate/2).
    uint32_t halfN = n / 2;
    float freqStep = config_.sampleRate / static_cast<float>(n);

    for (uint32_t k = 0; k < halfN; ++k) {
        float freq = k * freqStep;
        const auto& c = data[k];

        float val;
        switch (config_.type) {
            case SpectrumType::Magnitude: {
                float mag = std::abs(c) / static_cast<float>(halfN);
                if (config_.scale == SpectrumScale::dB) {
                    val = 20.0f * std::log10(mag + 1e-30f);
                } else {
                    val = mag;
                }
                break;
            }
            case SpectrumType::Phase:
            case SpectrumType::Angle: {
                // mpl _spectral_helper: 'angle' = raw np.angle, 'phase'
                // = np.unwrap(angle) along the frequency axis.
                val = std::atan2(c.imag(), c.real());
                break;
            }
        }

        freqs_.push_back(freq);
        values_.push_back(val);
    }

    if (config_.type == SpectrumType::Phase) {
        // np.unwrap: shift by 2π so consecutive phases differ by < π.
        for (size_t i = 1; i < values_.size(); ++i) {
            float d = values_[i] - values_[i - 1];
            if (d > float(M_PI))
                values_[i] -= 2.0f * float(M_PI) *
                              std::floor((d + float(M_PI)) /
                                         (2.0f * float(M_PI)));
            else if (d < -float(M_PI))
                values_[i] += 2.0f * float(M_PI) *
                              std::floor((float(M_PI) - d) /
                                         (2.0f * float(M_PI)));
        }
    }
}

void SpectrumPlot::prepare(render::Renderer& r) {
    computeSpectrum(&r);

    // Build line points.
    linePoints_.clear();
    for (size_t i = 0; i < freqs_.size(); ++i)
        linePoints_.push_back({freqs_[i], values_[i]});

    if (!lineRenderer_) lineRenderer_ = r.gpu().createLineRenderer();
    if (!linePoints_.empty()) {
        lineRenderer_->upload(std::span{linePoints_}, config_.color,
                             config_.lineWidth);
    }
    prepared_ = true;
}

void SpectrumPlot::draw(render::Cmd& cmd, render::Renderer& r,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || linePoints_.empty()) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    lineRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(linePoints_.size()));
}

void SpectrumPlot::contributeToAutoscale(Viewport& v) const {
    for (float f : freqs_) {
        v.x.min = std::min(v.x.min, f);
        v.x.max = std::max(v.x.max, f);
    }
    for (float val : values_) {
        v.y.min = std::min(v.y.min, val);
        v.y.max = std::max(v.y.max, val);
    }
}

} // namespace volcano::plot
