// volcano/plot/plots/CoherePlot.cpp — magnitude-squared coherence
#include "volcano/plot/plots/CoherePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

uint32_t nextPow2(uint32_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1; n |= n >> 2; n |= n >> 4;
    n |= n >> 8; n |= n >> 16;
    return n + 1;
}

uint32_t bitReverse(uint32_t x, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

} // namespace

CoherePlot::CoherePlot(std::vector<float> x, std::vector<float> y,
                       CohereConfig config)
    : signalX_(std::move(x)), signalY_(std::move(y)), config_(std::move(config)) {
    if (signalX_.size() != signalY_.size())
        throw std::invalid_argument("CoherePlot: x and y signals must have the same size");
}

void CoherePlot::fft(std::vector<std::complex<float>>& data) {
    uint32_t n = static_cast<uint32_t>(data.size());
    if (n <= 1) return;

    int bits = 0;
    for (uint32_t tmp = n; tmp > 1; tmp >>= 1) ++bits;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = bitReverse(i, bits);
        if (j > i) std::swap(data[i], data[j]);
    }

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

void CoherePlot::applyWindow(std::vector<std::complex<float>>& data,
                             const std::vector<float>& signal,
                             CohereConfig::Window window,
                             float& windowPower) {
    uint32_t n = static_cast<uint32_t>(data.size());
    uint32_t sigLen = static_cast<uint32_t>(signal.size());

    windowPower = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float sample = (i < sigLen) ? signal[i] : 0.0f;
        float w = 1.0f;
        if (i < sigLen) {
            float t = static_cast<float>(i) / static_cast<float>(sigLen - 1);
            switch (window) {
                case CohereConfig::Rectangular:
                    w = 1.0f;
                    break;
                case CohereConfig::Hann:
                    w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                    break;
                case CohereConfig::Hamming:
                    w = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                    break;
                case CohereConfig::Blackman:
                    w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                        + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                    break;
            }
        }
        data[i] = std::complex<float>(sample * w, 0.0f);
        windowPower += w * w;
    }
}

void CoherePlot::computeCoherence() {
    freqs_.clear();
    values_.clear();

    if (signalX_.empty() || signalY_.empty()) return;

    uint32_t maxLen = static_cast<uint32_t>(std::max(signalX_.size(), signalY_.size()));
    uint32_t n = config_.nfft > 0 ? config_.nfft : nextPow2(maxLen);
    if (n < 2) n = 2;

    std::vector<std::complex<float>> dataX(n), dataY(n);
    float winPowX = 0.0f, winPowY = 0.0f;
    applyWindow(dataX, signalX_, config_.window, winPowX);
    applyWindow(dataY, signalY_, config_.window, winPowY);

    if (winPowX < 1e-30f) winPowX = 1.0f;
    if (winPowY < 1e-30f) winPowY = 1.0f;

    fft(dataX);
    fft(dataY);

    uint32_t halfN = n / 2;
    float freqStep = config_.sampleRate / static_cast<float>(n);

    // Normalization factors.
    float normX = 1.0f / (config_.sampleRate * winPowX);
    float normY = 1.0f / (config_.sampleRate * winPowY);
    float normXY = 1.0f / (config_.sampleRate * std::sqrt(winPowX * winPowY));

    for (uint32_t k = 0; k < halfN; ++k) {
        float freq = k * freqStep;

        // Auto-power spectral densities.
        float pxx = std::norm(dataX[k]) * normX;  // |X|^2 * normX
        float pyy = std::norm(dataY[k]) * normY;  // |Y|^2 * normY

        // Cross-power spectral density (magnitude).
        std::complex<float> cross = dataX[k] * std::conj(dataY[k]);
        float pxy = std::abs(cross) * normXY;

        // One-sided correction: double for non-DC, non-Nyquist.
        if (k > 0 && k < halfN - 1) {
            pxx *= 2.0f;
            pyy *= 2.0f;
            pxy *= 2.0f;
        }

        // Coherence: |Pxy|^2 / (Pxx * Pyy)
        float denom = pxx * pyy;
        float coh = (denom < 1e-30f) ? 0.0f
                                     : (pxy * pxy) / denom;
        // Clamp to [0, 1].
        coh = std::clamp(coh, 0.0f, 1.0f);

        freqs_.push_back(freq);
        values_.push_back(coh);
    }
}

void CoherePlot::prepare(render::Renderer& r) {
    computeCoherence();

    linePoints_.clear();
    for (size_t i = 0; i < freqs_.size(); ++i)
        linePoints_.push_back({freqs_[i], values_[i]});

    auto& ctx = r.backend().context();
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

void CoherePlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void CoherePlot::contributeToAutoscale(Viewport& v) const {
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
