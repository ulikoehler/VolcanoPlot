// volcano/plot/plots/SpectrumPlot.cpp — magnitude/phase/angle spectrum implementation
#include "volcano/plot/plots/SpectrumPlot.hpp"
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

void SpectrumPlot::applyWindow(std::vector<std::complex<float>>& data) const {
    uint32_t n = static_cast<uint32_t>(data.size());
    uint32_t sigLen = static_cast<uint32_t>(signal_.size());

    // Copy signal into complex array and apply window.
    for (uint32_t i = 0; i < n; ++i) {
        float sample = (i < sigLen) ? signal_[i] : 0.0f;
        float w = 1.0f;
        if (i < sigLen) {
            float t = static_cast<float>(i) / static_cast<float>(sigLen - 1);
            switch (config_.window) {
                case SpectrumConfig::Rectangular:
                    w = 1.0f;
                    break;
                case SpectrumConfig::Hann:
                    w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                    break;
                case SpectrumConfig::Hamming:
                    w = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                    break;
                case SpectrumConfig::Blackman:
                    w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                        + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                    break;
            }
        }
        data[i] = std::complex<float>(sample * w, 0.0f);
    }
}

void SpectrumPlot::computeSpectrum() {
    freqs_.clear();
    values_.clear();

    if (signal_.empty()) return;

    // Zero-pad to next power of 2.
    uint32_t n = nextPow2(static_cast<uint32_t>(signal_.size()));
    if (n < 2) n = 2;

    std::vector<std::complex<float>> data(n);
    applyWindow(data);

    fft(data);

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
                // Only compute phase where magnitude is significant.
                float mag = std::abs(c);
                if (mag < 1e-10f * static_cast<float>(halfN)) {
                    val = 0.0f;
                } else {
                    val = std::atan2(c.imag(), c.real());
                }
                break;
            }
        }

        freqs_.push_back(freq);
        values_.push_back(val);
    }
}

void SpectrumPlot::prepare(render::Renderer& r) {
    computeSpectrum();
    auto& ctx = r.backend().context();

    // Build line points.
    linePoints_.clear();
    for (size_t i = 0; i < freqs_.size(); ++i)
        linePoints_.push_back({freqs_[i], values_[i]});

    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!linePoints_.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{linePoints_}, config_.color,
                             config_.lineWidth);
    }
    prepared_ = true;
}

void SpectrumPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || linePoints_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(linePoints_.size()));
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
