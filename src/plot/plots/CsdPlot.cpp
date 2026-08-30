// volcano/plot/plots/CsdPlot.cpp — cross-spectral density implementation
#include "volcano/plot/plots/CsdPlot.hpp"
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

CsdPlot::CsdPlot(std::vector<float> x, std::vector<float> y, CsdConfig config)
    : signalX_(std::move(x)), signalY_(std::move(y)), config_(std::move(config)) {
    if (signalX_.size() != signalY_.size())
        throw std::invalid_argument("CsdPlot: x and y signals must have the same size");
}

void CsdPlot::fft(std::vector<std::complex<float>>& data) {
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

void CsdPlot::applyWindow(std::vector<std::complex<float>>& data,
                          const std::vector<float>& signal,
                          CsdConfig::Window window, float& windowPower) {
    uint32_t n = static_cast<uint32_t>(data.size());
    uint32_t sigLen = static_cast<uint32_t>(signal.size());

    windowPower = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float sample = (i < sigLen) ? signal[i] : 0.0f;
        float w = 1.0f;
        if (i < sigLen) {
            float t = static_cast<float>(i) / static_cast<float>(sigLen - 1);
            switch (window) {
                case CsdConfig::Rectangular:
                    w = 1.0f;
                    break;
                case CsdConfig::Hann:
                    w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                    break;
                case CsdConfig::Hamming:
                    w = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                    break;
                case CsdConfig::Blackman:
                    w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                        + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                    break;
            }
        }
        data[i] = std::complex<float>(sample * w, 0.0f);
        windowPower += w * w;
    }
}

void CsdPlot::computeCsd() {
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

    // Average window power (for cross-spectrum, use geometric mean of the two).
    float windowPower = std::sqrt(winPowX * winPowY);
    if (windowPower < 1e-30f) windowPower = 1.0f;

    fft(dataX);
    fft(dataY);

    uint32_t halfN = n / 2;
    float freqStep = config_.sampleRate / static_cast<float>(n);

    // Normalization: CSD = |X * conj(Y)| / (sampleRate * windowPower)
    float norm = 1.0f / (config_.sampleRate * windowPower);

    for (uint32_t k = 0; k < halfN; ++k) {
        float freq = k * freqStep;
        // Cross-spectrum: X(k) * conj(Y(k))
        std::complex<float> cross = dataX[k] * std::conj(dataY[k]);
        float power = std::abs(cross) * norm;

        // One-sided correction: double the power for non-DC, non-Nyquist bins.
        if (k > 0 && k < halfN - 1)
            power *= 2.0f;

        float db = 10.0f * std::log10(power + 1e-30f);

        freqs_.push_back(freq);
        values_.push_back(db);
    }
}

void CsdPlot::prepare(render::Renderer& r) {
    computeCsd();

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

void CsdPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void CsdPlot::contributeToAutoscale(Viewport& v) const {
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
