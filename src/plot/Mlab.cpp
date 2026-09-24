/// @file Mlab.cpp — matplotlib.mlab numerical helpers.
///
/// Formulas mirror matplotlib/mlab.py (_spectral_helper, detrend,
/// window_*, stride_windows, prctile, bivariate_normal, PCA) and
/// scipy's gaussian_kde for the KDE class.

#include "volcano/plot/Mlab.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <random>
#include <stdexcept>

namespace volcano::plot::mlab {
namespace {

constexpr float kPi = 3.14159265358979323846f;

uint32_t nextPow2(uint32_t n) {
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

uint32_t bitReverse(uint32_t v, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

/// Iterative radix-2 FFT (size must be a power of two).
void fftPow2(std::vector<std::complex<float>>& data) {
    const uint32_t n = static_cast<uint32_t>(data.size());
    if (n <= 1) return;
    int bits = 0;
    for (uint32_t tmp = n; tmp > 1; tmp >>= 1) ++bits;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = bitReverse(i, bits);
        if (j > i) std::swap(data[i], data[j]);
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * kPi / static_cast<float>(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (uint32_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (uint32_t k = 0; k < len / 2; ++k) {
                auto u = data[i + k];
                auto t = w * data[i + k + len / 2];
                data[i + k] = u + t;
                data[i + k + len / 2] = u - t;
                w *= wlen;
            }
        }
    }
}

/// Bluestein FFT for arbitrary n (via pow2 convolution).
std::vector<std::complex<float>> fftN(
    const std::vector<std::complex<float>>& x) {
    const size_t n = x.size();
    if (n == nextPow2(uint32_t(n))) {
        auto d = x;
        fftPow2(d);
        return d;
    }
    const size_t m = nextPow2(uint32_t(2 * n - 1));
    std::vector<std::complex<float>> a(m, {0.f, 0.f}), b(m, {0.f, 0.f});
    for (size_t k = 0; k < n; ++k) {
        float ph = kPi * float((k * k) % (2 * n)) / float(n);
        std::complex<float> e(std::cos(ph), -std::sin(ph));
        a[k] = x[k] * e;
        b[k] = std::conj(e);
        if (k > 0) b[m - k] = std::conj(e);
    }
    fftPow2(a);
    fftPow2(b);
    for (size_t k = 0; k < m; ++k) a[k] *= b[k];
    // inverse fft via conjugation trick.
    for (auto& v : a) v = std::conj(v);
    fftPow2(a);
    const float inv = 1.0f / static_cast<float>(m);
    std::vector<std::complex<float>> out(n);
    for (size_t k = 0; k < n; ++k) {
        float ph = kPi * float((k * k) % (2 * n)) / float(n);
        std::complex<float> e(std::cos(ph), -std::sin(ph));
        out[k] = std::conj(a[k]) * inv * e;
    }
    return out;
}

/// Window values for a named window (mpl window_* functions).
std::vector<float> windowVals(std::string_view name, uint32_t n,
                              bool* ok) {
    *ok = true;
    std::vector<float> w(n);
    if (n == 0) return w;
    for (uint32_t i = 0; i < n; ++i) {
        float t = n > 1 ? float(i) / float(n - 1) : 0.0f;
        if (name == "hann" || name == "hanning")
            w[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * t));
        else if (name == "hamming")
            w[i] = 0.54f - 0.46f * std::cos(2.0f * kPi * t);
        else if (name == "blackman")
            w[i] = 0.42f - 0.5f * std::cos(2.0f * kPi * t)
                   + 0.08f * std::cos(4.0f * kPi * t);
        else if (name == "bartlett")
            w[i] = 1.0f - std::abs((i - 0.5f * (n - 1)) /
                                   (0.5f * (n - 1) + 1e-30f));
        else if (name == "none" || name == "rect" ||
                 name == "rectangular" || name == "boxcar")
            w[i] = 1.0f;
        else { *ok = false; return {}; }
    }
    return w;
}

/// Jacobi eigenvalue solver for symmetric d×d (row-major). Returns
/// eigenvalues (descending) and eigenvectors as rows.
void jacobiEig(std::vector<float> A, uint32_t d,
               std::vector<float>& evals,
               std::vector<std::vector<float>>& evecs) {
    evecs.assign(d, std::vector<float>(d, 0.0f));
    for (uint32_t i = 0; i < d; ++i) evecs[i][i] = 1.0f;
    for (int sweep = 0; sweep < 100; ++sweep) {
        float off = 0.0f;
        for (uint32_t p = 0; p < d; ++p)
            for (uint32_t q = p + 1; q < d; ++q)
                off += A[p * d + q] * A[p * d + q];
        if (off < 1e-20f) break;
        for (uint32_t p = 0; p < d; ++p) {
            for (uint32_t q = p + 1; q < d; ++q) {
                float app = A[p * d + p], aqq = A[q * d + q],
                      apq = A[p * d + q];
                if (std::abs(apq) < 1e-30f) continue;
                float phi = 0.5f * std::atan2(2.0f * apq, aqq - app);
                float c = std::cos(phi), s = std::sin(phi);
                for (uint32_t k = 0; k < d; ++k) {
                    float akp = A[k * d + p], akq = A[k * d + q];
                    A[k * d + p] = c * akp - s * akq;
                    A[k * d + q] = s * akp + c * akq;
                }
                for (uint32_t k = 0; k < d; ++k) {
                    float apk = A[p * d + k], aqk = A[q * d + k];
                    A[p * d + k] = c * apk - s * aqk;
                    A[q * d + k] = s * apk + c * aqk;
                }
                for (uint32_t k = 0; k < d; ++k) {
                    float vkp = evecs[k][p], vkq = evecs[k][q];
                    evecs[k][p] = c * vkp - s * vkq;
                    evecs[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    evals.resize(d);
    for (uint32_t i = 0; i < d; ++i) evals[i] = A[i * d + i];
    // Sort descending (evecs are column eigenvectors → transpose).
    std::vector<uint32_t> ord(d);
    std::iota(ord.begin(), ord.end(), 0u);
    std::ranges::sort(ord, [&](uint32_t a, uint32_t b) {
        return evals[a] > evals[b];
    });
    std::vector<float> se(d);
    std::vector<std::vector<float>> sv(d, std::vector<float>(d));
    for (uint32_t i = 0; i < d; ++i) {
        se[i] = evals[ord[i]];
        for (uint32_t j = 0; j < d; ++j) sv[i][j] = evecs[j][ord[i]];
    }
    evals = se; evecs = sv;
}

/// Cholesky factorization of symmetric positive-definite d×d → lower
/// triangular L (row-major, zeros above diagonal). Adds jitter when
/// the matrix is (nearly) singular, like scipy's linCholesky fallback.
std::vector<float> cholesky(std::vector<float> A, uint32_t d) {
    std::vector<float> L(d * d, 0.0f);
    for (int attempt = 0; attempt < 8; ++attempt) {
        bool ok = true;
        for (uint32_t i = 0; i < d && ok; ++i) {
            for (uint32_t j = 0; j <= i; ++j) {
                float s = A[i * d + j];
                for (uint32_t k = 0; k < j; ++k)
                    s -= L[i * d + k] * L[j * d + k];
                if (i == j) {
                    if (s <= 0.0f) { ok = false; break; }
                    L[i * d + i] = std::sqrt(s);
                } else {
                    L[i * d + j] = s / L[j * d + j];
                }
            }
        }
        if (ok) return L;
        // Jitter the diagonal and retry.
        float eps = 1e-6f;
        for (uint32_t i = 0; i < d; ++i)
            eps = std::max(eps, 1e-6f * std::abs(A[i * d + i]));
        for (uint32_t i = 0; i < d; ++i) A[i * d + i] += eps;
    }
    // Fall back to diagonal.
    std::ranges::fill(L, 0.0f);
    for (uint32_t i = 0; i < d; ++i)
        L[i * d + i] = std::sqrt(std::max(A[i * d + i], 1e-8f));
    return L;
}

/// Invert lower-triangular L → returns L⁻¹ (d×d row-major).
std::vector<float> invLower(const std::vector<float>& L, uint32_t d) {
    std::vector<float> inv(d * d, 0.0f);
    for (uint32_t i = 0; i < d; ++i) {
        inv[i * d + i] = 1.0f / L[i * d + i];
        for (uint32_t j = i + 1; j < d; ++j) {
            float s = 0.0f;
            for (uint32_t k = i; k < j; ++k)
                s += L[j * d + k] * inv[k * d + i];
            inv[j * d + i] = -s / L[j * d + j];
        }
    }
    return inv;
}

} // namespace

Detrend detrendKey(std::string_view key) {
    if (key == "none") return Detrend::None;
    if (key == "linear") return Detrend::Linear;
    if (key == "mean" || key == "constant" || key == "default" ||
        key.empty())
        return Detrend::Mean;
    throw std::invalid_argument(
        std::format("unrecognized detrend key '{}'", std::string(key)));
    return Detrend::None;  // unreachable — placates the compiler
}

std::vector<float> detrendNone(std::span<const float> x) {
    return {x.begin(), x.end()};
}

std::vector<float> detrendMean(std::span<const float> x) {
    if (x.empty()) return {};
    double m = std::accumulate(x.begin(), x.end(), 0.0) /
               double(x.size());
    std::vector<float> out(x.size());
    for (size_t i = 0; i < x.size(); ++i) out[i] = x[i] - float(m);
    return out;
}

std::vector<float> detrendLinear(std::span<const float> y) {
    const size_t n = y.size();
    if (n < 2) return {y.begin(), y.end()};
    // mpl detrend_linear: lstsq on [x, 1] → subtract a*x + b.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sx += double(i); sy += y[i];
        sxx += double(i) * double(i); sxy += double(i) * y[i];
    }
    double det = double(n) * sxx - sx * sx;
    double a = det != 0 ? (double(n) * sxy - sx * sy) / det : 0.0;
    double b = (sy - a * sx) / double(n);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = y[i] - float(a * double(i) + b);
    return out;
}

std::vector<float> detrend(std::span<const float> x, Detrend key) {
    switch (key) {
    case Detrend::Mean: return detrendMean(x);
    case Detrend::Linear: return detrendLinear(x);
    default: return detrendNone(x);
    }
}

std::vector<float> windowNone(std::span<const float> x) {
    return {x.begin(), x.end()};
}
std::vector<float> windowHanning(std::span<const float> x) {
    std::vector<float> out(x.size());
    bool ok;
    auto w = windowVals("hann", uint32_t(x.size()), &ok);
    for (size_t i = 0; i < x.size(); ++i) out[i] = x[i] * w[i];
    return out;
}
std::vector<float> windowHamming(std::span<const float> x) {
    std::vector<float> out(x.size());
    bool ok;
    auto w = windowVals("hamming", uint32_t(x.size()), &ok);
    for (size_t i = 0; i < x.size(); ++i) out[i] = x[i] * w[i];
    return out;
}
std::vector<float> windowBlackman(std::span<const float> x) {
    std::vector<float> out(x.size());
    bool ok;
    auto w = windowVals("blackman", uint32_t(x.size()), &ok);
    for (size_t i = 0; i < x.size(); ++i) out[i] = x[i] * w[i];
    return out;
}
std::vector<float> windowByName(std::string_view name, size_t n) {
    bool ok;
    auto w = windowVals(name, uint32_t(n), &ok);
    if (!ok)
        throw std::invalid_argument(
            std::format("unrecognized window '{}'", std::string(name)));
    return w;
}

std::vector<float> applyWindow(std::span<const float> x,
                               std::span<const float> window) {
    if (window.empty())
        throw std::invalid_argument("apply_window: empty window");
    std::vector<float> out(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        out[i] = x[i] * window[i % window.size()];
    return out;
}

std::vector<std::vector<float>> strideWindows(
    std::span<const float> x, uint32_t n, uint32_t noverlap) {
    // mpl stride_windows: step = n - noverlap.
    uint32_t step = n - std::min(noverlap, n - 1);
    std::vector<std::vector<float>> out;
    if (x.size() < n || step == 0) return out;
    for (size_t off = 0; off + n <= x.size(); off += step)
        out.emplace_back(x.begin() + off, x.begin() + off + n);
    return out;
}

std::vector<float> prctile(std::span<const float> x,
                           std::span<const float> p) {
    std::vector<float> s(x.begin(), x.end());
    std::ranges::sort(s);
    std::vector<float> out;
    out.reserve(p.size());
    for (float v : p) {
        if (s.empty()) { out.push_back(0.0f); continue; }
        float rank = v / 100.0f * float(s.size() - 1);
        size_t lo = size_t(rank);
        size_t hi = std::min(lo + 1, s.size() - 1);
        float f = rank - float(lo);
        out.push_back(s[lo] * (1.0f - f) + s[hi] * f);
    }
    return out;
}

std::vector<float> bivariateNormal(
    std::span<const float> xs, std::span<const float> ys,
    float sigmax, float sigmay, float mux, float muy, float sigmaxy) {
    // mpl mlab.bivariate_normal.
    float rho = sigmaxy / (sigmax * sigmay);
    float rho2 = rho * rho;
    float denom = 2.0f * kPi * sigmax * sigmay *
                  std::sqrt(std::max(1.0f - rho2, 1e-30f));
    std::vector<float> out(ys.size() * xs.size());
    for (size_t j = 0; j < ys.size(); ++j) {
        for (size_t i = 0; i < xs.size(); ++i) {
            float dx = xs[i] - mux, dy = ys[j] - muy;
            float z = dx * dx / (sigmax * sigmax) +
                      dy * dy / (sigmay * sigmay) -
                      2.0f * rho * dx * dy / (sigmax * sigmay);
            out[j * xs.size() + i] =
                std::exp(-z / (2.0f * std::max(1.0f - rho2, 1e-30f))) /
                denom;
        }
    }
    return out;
}

void fft(std::vector<std::complex<float>>& data) {
    auto out = fftN(data);
    std::copy(out.begin(), out.end(), data.begin());
}

SpectralResult spectralHelper(std::span<const float> x,
                              std::span<const float> y,
                              const SpectralConfig& cfg) {
    const bool sameData = y.empty();
    const uint32_t nfft = cfg.nfft > 0 ? cfg.nfft : 256;
    if (cfg.noverlap >= nfft)
        throw std::invalid_argument("noverlap must be less than NFFT");
    const uint32_t padTo = cfg.padTo > 0 ? cfg.padTo : nfft;
    const std::string mode = cfg.mode.empty() ? "psd" : cfg.mode;
    if (!sameData && mode != "psd")
        throw std::invalid_argument(
            "x and y must be equal if mode is not 'psd'");

    // mpl zero-pads short signals to NFFT.
    std::vector<float> xs(x.begin(), x.end());
    if (xs.size() < nfft) xs.resize(nfft, 0.0f);
    std::vector<float> ys(y.begin(), y.end());
    if (!sameData && ys.size() < nfft) ys.resize(nfft, 0.0f);

    bool scaleByFreq = mode == "psd" ? cfg.scaleByFreq : false;
    const uint32_t numFreqs =
        padTo % 2 ? (padTo + 1) / 2 : padTo / 2 + 1;
    const float scalingFactor = 2.0f;

    bool wok = true;
    auto win = cfg.winVals.empty()
        ? windowVals(cfg.window, nfft, &wok) : cfg.winVals;
    if (!wok)
        throw std::invalid_argument(std::format(
            "unrecognized window '{}'", cfg.window));
    if (win.size() < nfft) win.resize(nfft, 0.0f);
    float winSum = 0.0f, winPow = 0.0f;
    for (float w : win) { winSum += w; winPow += w * w; }
    if (std::abs(winSum) < 1e-30f) winSum = 1.0f;
    if (winPow < 1e-30f) winPow = 1.0f;

    // Segments via sliding windows (mpl sliding_window_view stride).
    const uint32_t step = nfft - cfg.noverlap;
    const uint32_t numSegs =
        xs.size() >= nfft
            ? uint32_t((xs.size() - nfft) / step + 1) : 1;

    SpectralResult R;
    R.numFreqs = numFreqs;
    R.numSegs = numSegs;
    R.values.assign(size_t(numFreqs) * numSegs, {0.f, 0.f});

    auto segSpectrum = [&](const std::vector<float>& sig,
                           uint32_t seg,
                           std::vector<std::complex<float>>& out) {
        auto view = std::span<const float>(
            sig.data() + seg * step, nfft);
        auto segData = cfg.detrendFn
            ? cfg.detrendFn(view) : detrend(view, cfg.detrend);
        std::vector<std::complex<float>> d(padTo, {0.f, 0.f});
        uint32_t take = std::min(nfft, padTo);
        for (uint32_t i = 0; i < take; ++i)
            d[i] = std::complex<float>(segData[i] * win[i], 0.0f);
        out = fftN(d);
        out.resize(numFreqs);
    };

    std::vector<std::complex<float>> fx, fy;
    for (uint32_t s = 0; s < numSegs; ++s) {
        segSpectrum(xs, s, fx);
        if (!sameData) segSpectrum(ys, s, fy);
        for (uint32_t k = 0; k < numFreqs; ++k) {
            auto& v = R.values[size_t(k) * numSegs + s];
            if (!sameData) v = std::conj(fx[k]) * fy[k];
            else if (mode == "psd") v = std::conj(fx[k]) * fx[k];
            else if (mode == "magnitude") v = std::abs(fx[k]) / winSum;
            else if (mode == "angle" || mode == "phase")
                v = std::arg(fx[k]);
            else if (mode == "complex") v = fx[k] / winSum;
        }
    }

    if (mode == "psd") {
        // mpl: scale all but DC (and NFFT/2 for even NFFT).
        uint32_t hi = nfft % 2 == 0 ? numFreqs - 1 : numFreqs;
        for (uint32_t k = 1; k < hi; ++k)
            for (uint32_t s = 0; s < numSegs; ++s)
                R.values[size_t(k) * numSegs + s] *= scalingFactor;
        float denom = scaleByFreq ? cfg.Fs * winPow
                                  : winSum * winSum;
        for (auto& v : R.values) v /= denom;
    }

    // mpl t = arange(NFFT/2, len-NFFT/2+1, NFFT-noverlap)/Fs.
    R.freqs.resize(numFreqs);
    for (uint32_t k = 0; k < numFreqs; ++k)
        R.freqs[k] = float(k) * cfg.Fs / float(padTo);
    R.t.resize(numSegs);
    for (uint32_t s = 0; s < numSegs; ++s)
        R.t[s] = (0.5f * float(nfft) + float(s) * float(step)) / cfg.Fs;

    // mpl phase unwrap across frequency (per segment column... mpl
    // unwraps axis=0 — down each frequency column).
    if (mode == "phase") {
        for (uint32_t s = 0; s < numSegs; ++s) {
            float prev = R.values[s].real(), off = 0.0f;
            for (uint32_t k = 1; k < numFreqs; ++k) {
                float cur = R.values[size_t(k) * numSegs + s].real();
                float d = cur - prev;
                if (d > kPi) off -= 2.0f * kPi;
                else if (d < -kPi) off += 2.0f * kPi;
                R.values[size_t(k) * numSegs + s] = cur + off;
                prev = cur;
            }
        }
    }
    return R;
}

std::pair<std::vector<float>, std::vector<float>> psd(
    std::span<const float> x, const SpectralConfig& cfg) {
    SpectralConfig c = cfg;
    c.mode = "psd";
    auto r = spectralHelper(x, {}, c);
    std::vector<float> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k) {
        double acc = 0.0;
        for (uint32_t s = 0; s < r.numSegs; ++s)
            acc += r.values[size_t(k) * r.numSegs + s].real();
        out[k] = float(acc / double(r.numSegs));
    }
    return {std::move(out), std::move(r.freqs)};
}

std::pair<std::vector<std::complex<float>>, std::vector<float>> csd(
    std::span<const float> x, std::span<const float> y,
    const SpectralConfig& cfg) {
    SpectralConfig c = cfg;
    c.mode = "psd";
    auto r = spectralHelper(x, y, c);
    std::vector<std::complex<float>> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k) {
        std::complex<double> acc{0.0, 0.0};
        for (uint32_t s = 0; s < r.numSegs; ++s)
            acc += std::complex<double>(
                r.values[size_t(k) * r.numSegs + s]);
        out[k] = std::complex<float>(acc / double(r.numSegs));
    }
    return {std::move(out), std::move(r.freqs)};
}

std::pair<std::vector<float>, std::vector<float>> cohere(
    std::span<const float> x, std::span<const float> y,
    const SpectralConfig& cfg) {
    if (x.size() < 2 * size_t(cfg.nfft ? cfg.nfft : 256))
        throw std::invalid_argument(
            "Coherence is calculated by averaging over *NFFT* length "
            "segments. Your signal is too short for your choice of "
            "*NFFT*.");
    auto [pxx, f1] = psd(x, cfg);
    auto [pyy, f2] = psd(y, cfg);
    auto [pxy, f3] = csd(x, y, cfg);
    std::vector<float> out(pxx.size());
    for (size_t k = 0; k < pxx.size(); ++k) {
        float d = pxx[k] * pyy[k];
        float n = std::norm(pxy[k]);
        out[k] = d > 0 ? n / d : 0.0f;
    }
    return {std::move(out), std::move(f1)};
}

SpectralResult specgram(std::span<const float> x,
                        const SpectralConfig& cfg) {
    SpectralConfig c = cfg;
    if (c.mode.empty() || c.mode == "default") c.mode = "psd";
    auto r = spectralHelper(x, {}, c);
    // mpl specgram returns magnitude for psd mode (already real) and
    // |result| otherwise — take abs for all modes.
    for (auto& v : r.values) v = std::abs(v);
    return r;
}

std::pair<std::vector<float>, std::vector<float>> magnitudeSpectrum(
    std::span<const float> x, float Fs, uint32_t padTo,
    std::string_view window) {
    SpectralConfig c;
    c.nfft = uint32_t(x.size());
    c.Fs = Fs; c.padTo = padTo; c.mode = "magnitude";
    c.scaleByFreq = false;
    c.window = std::string(window);
    auto r = spectralHelper(x, {}, c);
    std::vector<float> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k)
        out[k] = r.values[size_t(k) * r.numSegs].real();
    return {std::move(out), std::move(r.freqs)};
}

std::pair<std::vector<float>, std::vector<float>> angleSpectrum(
    std::span<const float> x, float Fs, uint32_t padTo,
    std::string_view window) {
    SpectralConfig c;
    c.nfft = uint32_t(x.size());
    c.Fs = Fs; c.padTo = padTo; c.mode = "angle";
    c.scaleByFreq = false;
    c.window = std::string(window);
    auto r = spectralHelper(x, {}, c);
    std::vector<float> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k)
        out[k] = r.values[size_t(k) * r.numSegs].real();
    return {std::move(out), std::move(r.freqs)};
}

std::pair<std::vector<float>, std::vector<float>> phaseSpectrum(
    std::span<const float> x, float Fs, uint32_t padTo,
    std::string_view window) {
    SpectralConfig c;
    c.nfft = uint32_t(x.size());
    c.Fs = Fs; c.padTo = padTo; c.mode = "phase";
    c.scaleByFreq = false;
    c.window = std::string(window);
    auto r = spectralHelper(x, {}, c);
    std::vector<float> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k)
        out[k] = r.values[size_t(k) * r.numSegs].real();
    return {std::move(out), std::move(r.freqs)};
}

std::pair<std::vector<std::complex<float>>, std::vector<float>>
complexSpectrum(std::span<const float> x, float Fs, uint32_t padTo,
                std::string_view window) {
    SpectralConfig c;
    c.nfft = uint32_t(x.size());
    c.Fs = Fs; c.padTo = padTo; c.mode = "complex";
    c.scaleByFreq = false;
    c.window = std::string(window);
    auto r = spectralHelper(x, {}, c);
    std::vector<std::complex<float>> out(r.numFreqs);
    for (uint32_t k = 0; k < r.numFreqs; ++k)
        out[k] = r.values[size_t(k) * r.numSegs];
    return {std::move(out), std::move(r.freqs)};
}

Pca pca(std::span<const float> a, uint32_t numVars) {
    const uint32_t d = numVars;
    const uint32_t n = d > 0 ? uint32_t(a.size() / d) : 0;
    if (n == 0 || d == 0)
        throw std::invalid_argument("pca: empty data");
    Pca p;
    p.mu.assign(d, 0.0f);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j) p.mu[j] += a[i * d + j];
    for (auto& m : p.mu) m /= float(n);
    // Centered data.
    std::vector<float> c(n * d);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j)
            c[i * d + j] = a[i * d + j] - p.mu[j];
    p.sigma.assign(d, 0.0f);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j)
            p.sigma[j] += c[i * d + j] * c[i * d + j];
    for (auto& s : p.sigma) s = std::sqrt(s / float(n));
    // Covariance = CᵀC/(n-1) → Jacobi eigendecomposition.
    std::vector<float> cov(d * d, 0.0f);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j)
            for (uint32_t k = 0; k <= j; ++k) {
                cov[j * d + k] += c[i * d + j] * c[i * d + k];
            }
    for (uint32_t j = 0; j < d; ++j)
        for (uint32_t k = 0; k <= j; ++k) {
            cov[j * d + k] /= float(n - 1);
            cov[k * d + j] = cov[j * d + k];
        }
    jacobiEig(cov, d, p.evals, p.evecs);
    float total = std::accumulate(p.evals.begin(), p.evals.end(), 0.0f);
    if (total <= 0) total = 1.0f;
    p.fracs.resize(d);
    for (uint32_t i = 0; i < d; ++i) p.fracs[i] = p.evals[i] / total;
    return p;
}

std::vector<float> Pca::project(std::span<const float> a) const {
    uint32_t d = uint32_t(mu.size());
    uint32_t n = uint32_t(a.size() / d);
    std::vector<float> out(size_t(n) * d, 0.0f);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t c = 0; c < d; ++c) {
            float acc = 0.0f;
            for (uint32_t j = 0; j < d; ++j)
                acc += (a[i * d + j] - mu[j]) * evecs[c][j];
            out[i * d + c] = acc;
        }
    return out;
}

// ── GaussianKDE (scipy gaussian_kde semantics) ───────────────────────

GaussianKde::GaussianKde(std::vector<float> dataset, uint32_t dims,
                         std::vector<float> weights)
    : data_(std::move(dataset)), weights_(std::move(weights)),
      dims_(dims) {
    n_ = dims_ > 0 ? uint32_t(data_.size() / dims_) : 0;
    if (weights_.empty()) weights_.assign(n_, 1.0f);
    scottsFactor();
}

void GaussianKde::scottsFactor() {
    factor_ = std::pow(float(n_), -1.0f / (float(dims_) + 4.0f));
    computeCovariance();
}

void GaussianKde::silvermanFactor() {
    factor_ = std::pow(float(n_) * (float(dims_) + 2.0f) / 4.0f,
                       -1.0f / (float(dims_) + 4.0f));
    computeCovariance();
}

void GaussianKde::setBandwidth(float factor) {
    factor_ = factor;
    computeCovariance();
}

void GaussianKde::computeCovariance() {
    // Weighted covariance of the dataset (dims-major stats).
    const uint32_t d = dims_, n = n_;
    std::vector<float> mean(d, 0.0f);
    float wsum = 0.0f;
    for (uint32_t i = 0; i < n; ++i) wsum += weights_[i];
    if (wsum <= 0) wsum = 1.0f;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j)
            mean[j] += weights_[i] * data_[i * d + j];
    for (auto& m : mean) m /= wsum;
    std::vector<float> cov(d * d, 0.0f);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < d; ++j) {
            float dj = data_[i * d + j] - mean[j];
            for (uint32_t k = 0; k <= j; ++k)
                cov[j * d + k] += weights_[i] * dj *
                                  (data_[i * d + k] - mean[k]);
        }
    // Weighted sample covariance (ddof=1 like scipy).
    float wsq = 0.0f;
    for (float w : weights_) wsq += w * w;
    float norm = wsum - wsq / wsum;
    if (norm <= 0) norm = 1.0f;
    float f2 = factor_ * factor_;
    for (uint32_t j = 0; j < d; ++j)
        for (uint32_t k = 0; k <= j; ++k) {
            cov[j * d + k] = cov[j * d + k] / norm * f2;
            cov[k * d + j] = cov[j * d + k];
        }
    cov_ = cov;
    auto L = cholesky(cov, d);
    auto Linv = invLower(L, d);
    // covInv = (L⁻¹)ᵀ L⁻¹
    covInv_.assign(d * d, 0.0f);
    for (uint32_t j = 0; j < d; ++j)
        for (uint32_t k = 0; k < d; ++k)
            for (uint32_t i = std::max(j, k); i < d; ++i)
                covInv_[j * d + k] += Linv[i * d + j] * Linv[i * d + k];
    // det(cov) = prod(diag L)²
    float det = 1.0f;
    for (uint32_t i = 0; i < d; ++i) det *= L[i * d + i];
    det *= det;
    normConst_ = std::sqrt(std::pow(2.0f * kPi, float(d)) * det);
    if (normConst_ <= 0) normConst_ = 1.0f;
}

std::vector<float> GaussianKde::evaluate(
    std::span<const float> points, uint32_t numPoints) const {
    const uint32_t d = dims_, n = n_;
    std::vector<float> out(numPoints, 0.0f);
    for (uint32_t p = 0; p < numPoints; ++p) {
        double acc = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            // d² = (x-μ)ᵀ Σ⁻¹ (x-μ)
            double d2 = 0.0;
            for (uint32_t j = 0; j < d; ++j) {
                double acc2 = 0.0;
                for (uint32_t k = 0; k < d; ++k)
                    acc2 += covInv_[j * d + k] *
                            (points[p * d + k] - data_[i * d + k]);
                d2 += (points[p * d + j] - data_[i * d + j]) * acc2;
            }
            acc += weights_[i] * std::exp(-0.5 * d2);
        }
        float wsum = 0.0f;
        for (float w : weights_) wsum += w;
        out[p] = float(acc / (normConst_ * (wsum > 0 ? wsum : 1.0f)));
    }
    return out;
}

std::vector<float> GaussianKde::resample(uint32_t size,
                                       uint64_t seed) const {
    // mpl/scipy resample: cumulative-weight indexing + Gaussian noise.
    std::mt19937_64 rng(seed);
    std::vector<float> cumw(n_);
    float acc = 0.0f;
    for (uint32_t i = 0; i < n_; ++i) {
        acc += weights_[i];
        cumw[i] = acc;
    }
    if (acc <= 0) acc = 1.0f;
    for (auto& c : cumw) c /= acc;
    auto L = cholesky(cov_, dims_);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::vector<float> out(size_t(size) * dims_);
    for (uint32_t s = 0; s < size; ++s) {
        float u = uni(rng);
        uint32_t idx = uint32_t(
            std::lower_bound(cumw.begin(), cumw.end(), u) -
            cumw.begin());
        if (idx >= n_) idx = n_ - 1;
        std::vector<float> z(dims_);
        for (auto& v : z) v = gauss(rng);
        for (uint32_t j = 0; j < dims_; ++j) {
            float nz = 0.0f;
            for (uint32_t k = 0; k <= j; ++k)
                nz += L[j * dims_ + k] * z[k];
            out[s * dims_ + j] = data_[idx * dims_ + j] + nz;
        }
    }
    return out;
}

} // namespace volcano::plot::mlab
