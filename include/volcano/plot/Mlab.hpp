/// @file Mlab.hpp — matplotlib.mlab numerical helpers.
///
/// Implements mpl mlab's spectral (_spectral_helper), detrend, window,
/// and legacy array utilities with the same formulas as
/// matplotlib/mlab.py. Real-valued signals only (sides='onesided');
/// complex inputs are unsupported like the rest of VolcanoPlot.
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volcano::plot::mlab {

/// mpl detrend key.
enum class Detrend { None, Mean, Linear };

/// Parse mpl detrend keys: 'none'/'mean'/'constant'/'default'/'linear'.
[[nodiscard]] Detrend detrendKey(std::string_view key);

/// mpl detrend_none(x).
[[nodiscard]] std::vector<float> detrendNone(std::span<const float> x);
/// mpl detrend_mean(x) — subtract the mean.
[[nodiscard]] std::vector<float> detrendMean(std::span<const float> x);
/// mpl detrend_linear(y) — subtract the least-squares line.
[[nodiscard]] std::vector<float> detrendLinear(std::span<const float> y);
/// mpl detrend(x, key).
[[nodiscard]] std::vector<float> detrend(std::span<const float> x,
                                         Detrend key);

/// mpl window_none(x) — returns x unchanged.
[[nodiscard]] std::vector<float> windowNone(std::span<const float> x);
/// mpl window_hanning(x) — 0.5*(1-cos(2π·i/(n-1))).
[[nodiscard]] std::vector<float> windowHanning(std::span<const float> x);
/// Hamming / Blackman (our window enum's extra names).
[[nodiscard]] std::vector<float> windowHamming(std::span<const float> x);
[[nodiscard]] std::vector<float> windowBlackman(std::span<const float> x);
/// mpl window name → the same length-preserving window vector.
[[nodiscard]] std::vector<float> windowByName(std::string_view name,
                                              size_t n);

/// mpl apply_window(x, window) — element-wise multiply (1-D only).
[[nodiscard]] std::vector<float> applyWindow(
    std::span<const float> x, std::span<const float> window);

/// mpl stride_windows(x, n, noverlap) → segments of length n stepping
/// n-noverlap (noverlap default = n//2 in legacy mpl).
[[nodiscard]] std::vector<std::vector<float>> strideWindows(
    std::span<const float> x, uint32_t n, uint32_t noverlap);

/// mpl prctile(x, p) — linear-interpolated percentiles (numpy
/// 'linear' method, which is what legacy mlab used).
[[nodiscard]] std::vector<float> prctile(std::span<const float> x,
                                         std::span<const float> p);

/// mpl bivariate_normal(X, Y, sigmax, sigmay, mux, muy, sigmaxy) —
/// evaluates the 2-D Gaussian density over the X×Y meshgrid. `xs`/`ys`
/// are the axis vectors (mpl passes meshgrid matrices; the density is
/// separable per point so axis vectors suffice). Returns row-major
/// ys.size()×xs.size().
[[nodiscard]] std::vector<float> bivariateNormal(
    std::span<const float> xs, std::span<const float> ys,
    float sigmax = 1.0f, float sigmay = 1.0f,
    float mux = 0.0f, float muy = 0.0f, float sigmaxy = 0.0f);

/// mpl rk4(derivs, y0, t) — 4th-order Runge-Kutta integration of the
/// ODE y' = derivs(y, t) over the time samples `t`. `derivs` is
/// supplied by the caller (Python callable at binding level).
/// Returns y at each t (rows).
template <typename F>
[[nodiscard]] std::vector<std::vector<float>> rk4(
    F&& derivs, std::span<const float> y0, std::span<const float> t) {
    std::vector<std::vector<float>> out;
    const size_t n = y0.size();
    std::vector<float> y(y0.begin(), y0.end());
    out.push_back(y);
    for (size_t i = 1; i < t.size(); ++i) {
        float h = t[i] - t[i - 1];
        float t1 = t[i - 1], t2 = t[i - 1] + h * 0.5f, t3 = t[i];
        auto k1 = derivs(y, t1);
        std::vector<float> tmp(n);
        for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + h * 0.5f * k1[j];
        auto k2 = derivs(tmp, t2);
        for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + h * 0.5f * k2[j];
        auto k3 = derivs(tmp, t2);
        for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + h * k3[j];
        auto k4 = derivs(tmp, t3);
        for (size_t j = 0; j < n; ++j)
            y[j] += h / 6.0f * (k1[j] + 2.0f * k2[j] +
                                2.0f * k3[j] + k4[j]);
        out.push_back(y);
    }
    return out;
}

/// In-place radix-2 FFT (padded with zeros to the next power of two).
void fft(std::vector<std::complex<float>>& data);

/// mpl _spectral_helper configuration.
struct SpectralConfig {
    uint32_t nfft = 256;
    float Fs = 2.0f;
    Detrend detrend = Detrend::None;
    /// Window applied to each segment (length nfft).
    std::string window = "hann";
    /// Explicit window samples (mpl accepts window arrays); overrides
    /// `window` when non-empty.
    std::vector<float> winVals;
    /// Callable detrend (mpl accepts functions); overrides `detrend`.
    std::function<std::vector<float>(std::span<const float>)>
        detrendFn;
    uint32_t noverlap = 0;
    /// pad_to ≥ nfft (0 → nfft).
    uint32_t padTo = 0;
    /// mpl scale_by_freq — density units (psd/csd default true).
    bool scaleByFreq = true;
    /// 'psd' (default) | 'complex' | 'magnitude' | 'angle' | 'phase'.
    std::string mode = "psd";
};

/// mpl _spectral_helper result: per-frequency rows × segment columns.
struct SpectralResult {
    /// Complex matrix numFreqs × numSegments (real parts only for
    /// psd/angle/phase post-processing — kept complex for csd).
    std::vector<std::complex<float>> values;
    std::vector<float> freqs;
    std::vector<float> t;
    uint32_t numFreqs = 0, numSegs = 0;
};

/// mpl _spectral_helper(x, y=None or csd pair, ...).
[[nodiscard]] SpectralResult spectralHelper(
    std::span<const float> x, std::span<const float> y,
    const SpectralConfig& cfg);

/// mpl mlab.psd → (Pxx, freqs): mean |FFT|² density per frequency.
[[nodiscard]] std::pair<std::vector<float>, std::vector<float>> psd(
    std::span<const float> x, const SpectralConfig& cfg);

/// mpl mlab.csd → (Pxy complex, freqs).
[[nodiscard]] std::pair<std::vector<std::complex<float>>,
                        std::vector<float>>
csd(std::span<const float> x, std::span<const float> y,
    const SpectralConfig& cfg);

/// mpl mlab.cohere → (Cxy, freqs): |Pxy|²/(Pxx·Pyy).
[[nodiscard]] std::pair<std::vector<float>, std::vector<float>> cohere(
    std::span<const float> x, std::span<const float> y,
    const SpectralConfig& cfg);

/// mpl mlab.specgram → (spec, freqs, t): numFreqs × numSegs.
[[nodiscard]] SpectralResult specgram(std::span<const float> x,
                                      const SpectralConfig& cfg);

/// mpl magnitude_spectrum / angle_spectrum / phase_spectrum /
/// complex_spectrum — single-window spectra over the whole signal.
[[nodiscard]] std::pair<std::vector<float>, std::vector<float>>
magnitudeSpectrum(std::span<const float> x, float Fs, uint32_t padTo,
                  std::string_view window);
[[nodiscard]] std::pair<std::vector<float>, std::vector<float>>
angleSpectrum(std::span<const float> x, float Fs, uint32_t padTo,
              std::string_view window);
[[nodiscard]] std::pair<std::vector<float>, std::vector<float>>
phaseSpectrum(std::span<const float> x, float Fs, uint32_t padTo,
              std::string_view window);
[[nodiscard]] std::pair<std::vector<std::complex<float>>,
                        std::vector<float>>
complexSpectrum(std::span<const float> x, float Fs, uint32_t padTo,
                std::string_view window);

/// mpl mlab.PCA — principal component analysis via covariance
/// eigendecomposition (Jacobi). Input `a` is row-major numObs × numVars.
struct Pca {
    std::vector<float> mu;      ///< variable means
    std::vector<float> sigma;   ///< variable std devs
    std::vector<float> fracs;   ///< fraction of variance per component
    std::vector<float> evals;   ///< eigenvalues (descending)
    std::vector<std::vector<float>> evecs;  ///< eigenvectors (rows)
    /// Project `a` (same layout) onto the components.
    [[nodiscard]] std::vector<float> project(
        std::span<const float> a) const;
};
[[nodiscard]] Pca pca(std::span<const float> a, uint32_t numVars);

/// mpl mlab.GaussianKDE — multivariate kernel density estimate with
/// Gaussian kernels. `dataset` is row-major numPoints × numDims.
class GaussianKde {
public:
    explicit GaussianKde(std::vector<float> dataset, uint32_t dims,
                         std::vector<float> weights = {});
    /// mpl bw_method='scott' → n^(-1/(d+4)).
    void scottsFactor();
    /// mpl bw_method='silverman' → (n(d+2)/4)^(-1/(d+4)).
    void silvermanFactor();
    /// mpl set_bandwidth(bw_method).
    void setBandwidth(float factor);
    /// mpl evaluate(points) — points row-major numPoints × numDims.
    [[nodiscard]] std::vector<float> evaluate(
        std::span<const float> points, uint32_t numPoints) const;
    [[nodiscard]] uint32_t dims() const { return dims_; }
    [[nodiscard]] uint32_t n() const { return n_; }
    [[nodiscard]] float factor() const { return factor_; }
    [[nodiscard]] const std::vector<float>& covariance() const {
        return cov_;
    }
    /// mpl resample(size) — weighted sampling without replacement.
    [[nodiscard]] std::vector<float> resample(uint32_t size,
                                              uint64_t seed) const;
private:
    std::vector<float> data_;   ///< n × d
    std::vector<float> weights_;
    std::vector<float> cov_;    ///< d × d Cholesky (lower, packed rows)
    std::vector<float> covInv_; ///< d × d inverse covariance
    float factor_ = 0.0f;
    float normConst_ = 0.0f;
    uint32_t dims_ = 0, n_ = 0;
    void computeCovariance();
};

} // namespace volcano::plot::mlab
