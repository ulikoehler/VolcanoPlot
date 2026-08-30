// volcano/plot/plots/SpecgramPlot.cpp — spectrogram implementation
#include "volcano/plot/plots/SpecgramPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
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

SpecgramPlot::SpecgramPlot(std::vector<float> signal, SpecgramConfig config)
    : signal_(std::move(signal)), config_(std::move(config)) {
    if (config_.nfft == 0) config_.nfft = 256;
    if (config_.noverlap >= config_.nfft)
        throw std::invalid_argument("SpecgramPlot: noverlap must be < nfft");
}

Color SpecgramPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void SpecgramPlot::fft(std::vector<std::complex<float>>& data) {
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

void SpecgramPlot::computeSpecgram() {
    data_.clear();
    nrows_ = 0;
    ncols_ = 0;

    if (signal_.empty() || signal_.size() < config_.nfft) return;

    uint32_t nfft = config_.nfft;
    uint32_t hop = nfft - config_.noverlap;
    uint32_t sigLen = static_cast<uint32_t>(signal_.size());

    // Number of time windows.
    ncols_ = (sigLen - nfft) / hop + 1;
    if (ncols_ == 0) return;

    // One-sided spectrum: nfft/2 frequency bins.
    nrows_ = nfft / 2;

    // Precompute window coefficients.
    std::vector<float> win(nfft);
    for (uint32_t i = 0; i < nfft; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(nfft - 1);
        switch (config_.window) {
            case SpecgramConfig::Rectangular:
                win[i] = 1.0f;
                break;
            case SpecgramConfig::Hann:
                win[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                break;
            case SpecgramConfig::Hamming:
                win[i] = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * t);
                break;
            case SpecgramConfig::Blackman:
                win[i] = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * t)
                       + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * t);
                break;
        }
    }

    data_.resize(nrows_ * ncols_);

    for (uint32_t col = 0; col < ncols_; ++col) {
        uint32_t start = col * hop;

        std::vector<std::complex<float>> frame(nfft);
        for (uint32_t i = 0; i < nfft; ++i)
            frame[i] = std::complex<float>(signal_[start + i] * win[i], 0.0f);

        fft(frame);

        // One-sided magnitude in dB.
        for (uint32_t k = 0; k < nrows_; ++k) {
            float mag = std::abs(frame[k]) / static_cast<float>(nrows_);
            float db = 10.0f * std::log10(mag + 1e-30f);
            data_[k * ncols_ + col] = db;
        }
    }
}

void SpecgramPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    if (nrows_ == 0 || ncols_ == 0) return;

    // Compute value range.
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
    } else {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float v : data_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
        valueRange_ = {vmin, vmax};
    }

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Time axis: each column spans [col * hop / sampleRate, (col * hop + nfft) / sampleRate].
    // But for cell-based display, we use cell boundaries:
    //   x = [col * hop / sr, (col + 1) * hop / sr]  (simplified)
    // Frequency axis: row k → freq = k * sr / nfft
    //   y = [k * sr/nfft, (k+1) * sr/nfft]
    float freqStep = config_.sampleRate / static_cast<float>(config_.nfft);
    float timeStep = static_cast<float>(config_.nfft - config_.noverlap)
                     / config_.sampleRate;

    for (uint32_t k = 0; k < nrows_; ++k) {
        for (uint32_t col = 0; col < ncols_; ++col) {
            float val = data_[k * ncols_ + col];
            Color color;
            if (std::isnan(val)) {
                color = Color::transparent();
            } else {
                float t = (val - valueRange_.min) / vspan;
                t = std::clamp(t, 0.0f, 1.0f);
                color = cmap.sample(t);
            }
            if (color.a == 0.0f) continue;

            float x0 = static_cast<float>(col) * timeStep;
            float x1 = x0 + timeStep;
            float y0 = static_cast<float>(k) * freqStep;
            float y1 = y0 + freqStep;

            Point2D bl{x0, y0}, br{x1, y0}, tl{x0, y1}, tr{x1, y1};
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tr);
            fillPositions_.push_back(tl);
            for (int i = 0; i < 6; ++i) fillColors_.push_back(color);
        }
    }
}

void SpecgramPlot::prepare(render::Renderer& r) {
    computeSpecgram();
    buildGeometry();

    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void SpecgramPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void SpecgramPlot::contributeToAutoscale(Viewport& v) const {
    if (nrows_ == 0 || ncols_ == 0) return;
    float freqStep = config_.sampleRate / static_cast<float>(config_.nfft);
    float timeStep = static_cast<float>(config_.nfft - config_.noverlap)
                     / config_.sampleRate;
    v.x.min = std::min(v.x.min, 0.0f);
    v.x.max = std::max(v.x.max, static_cast<float>(ncols_) * timeStep);
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, static_cast<float>(nrows_) * freqStep);
}

} // namespace volcano::plot
