// volcano/plot/plots/PsdPlot.cpp — power spectral density implementation
#include "volcano/plot/plots/PsdPlot.hpp"
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

PsdPlot::PsdPlot(std::vector<float> signal, PsdConfig config)
    : signal_(std::move(signal)), config_(std::move(config)) {}

void PsdPlot::fft(std::vector<std::complex<float>>& data) {
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

void PsdPlot::applyWindow(std::vector<std::complex<float>>& data,
                          float& windowPower) const {
    uint32_t n = static_cast<uint32_t>(data.size());
    uint32_t sigLen = static_cast<uint32_t>(signal_.size());

    windowPower = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float sample = (i < sigLen) ? signal_[i] : 0.0f;
        float w = 1.0f;
        if (i < sigLen) {
            float t = static_cast<float>(i) / static_cast<float>(sigLen - 1);
            switch (config_.window) {
                case PsdConfig::Rectangular:
                    w = 1.0f;
                    break;
                case PsdConfig::Hann:
                    w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                    break;
                case PsdConfig::Hamming:
                    w = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                    break;
                case PsdConfig::Blackman:
                    w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                        + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                    break;
            }
        }
        data[i] = std::complex<float>(sample * w, 0.0f);
        windowPower += w * w;
    }
}

void PsdPlot::computePsd() {
    freqs_.clear();
    values_.clear();

    if (signal_.empty()) return;

    // mpl Welch averaging: NFFT-sized segments with `noverlap` overlap
    // (mpl default 0), Hann window each, then average |FFT|^2 over
    // segments (_spectral_helper with sides='one-sided', scale_by_freq →
    // density in 1/Hz).
    uint32_t n = config_.nfft > 0 ? config_.nfft : 256;
    if (n < 2) n = 2;
    const uint32_t len = static_cast<uint32_t>(signal_.size());
    const uint32_t nover = std::min(config_.noverlap, n > 1 ? n - 1 : 0);
    const uint32_t step = n - nover;
    const uint32_t numSegs = len < n ? 1 : (len - n) / step + 1;

    // Hann window (mpl window_hanning default) and its power Σw².
    std::vector<float> win(n);
    float winPow = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float t = float(i) / float(n - 1);
        switch (config_.window) {
        case PsdConfig::Rectangular: win[i] = 1.0f; break;
        case PsdConfig::Hann:
            win[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * t)); break;
        case PsdConfig::Hamming:
            win[i] = 0.54f - 0.46f * std::cos(2.0f * float(M_PI) * t); break;
        case PsdConfig::Blackman:
            win[i] = 0.42f - 0.5f * std::cos(2.0f * float(M_PI) * t)
                     + 0.08f * std::cos(4.0f * float(M_PI) * t); break;
        }
        winPow += win[i] * win[i];
    }
    if (winPow < 1e-30f) winPow = 1.0f;

    const uint32_t halfN = n / 2;
    std::vector<float> pxx(halfN + 1, 0.0f);
    std::vector<std::complex<float>> data(n);
    for (uint32_t s = 0; s < numSegs; ++s) {
        uint32_t off = s * step;
        for (uint32_t i = 0; i < n; ++i) {
            float smp = (off + i < len) ? signal_[off + i] : 0.0f;
            data[i] = std::complex<float>(smp * win[i], 0.0f);
        }
        fft(data);
        for (uint32_t k = 0; k <= halfN; ++k)
            pxx[k] += std::norm(data[k]);
    }

    float freqStep = config_.sampleRate / static_cast<float>(n);
    float norm = 1.0f / (config_.sampleRate * winPow * float(numSegs));
    for (uint32_t k = 0; k <= halfN; ++k) {
        float power = pxx[k] * norm;
        // One-sided: double all bins except DC and Nyquist.
        if (k > 0 && k < halfN) power *= 2.0f;
        freqs_.push_back(k * freqStep);
        values_.push_back(10.0f * std::log10(power + 1e-30f));
    }
}

void PsdPlot::prepare(render::Renderer& r) {
    computePsd();

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

void PsdPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_ || linePoints_.empty()) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(linePoints_.size()));
}

void PsdPlot::contributeToAutoscale(Viewport& v) const {
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
