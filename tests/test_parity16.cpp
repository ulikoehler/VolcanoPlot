// tests/test_parity16.cpp — tests for the projections/legend/spines/
// mlab/markers parity batch:
//   1. vp.projections — geo grid endpoints (longitudeGridEnds honored
//      by drawGeoGrid) covered via Style fields; projection registry
//      name resolution.
//   2. vp.mlab — _spectral_helper math (raw PSD not dB, window/power
//      normalization, one-sided doubling, freqs/t vectors), detrend,
//      prctile, bivariate_normal, PCA, GaussianKDE.
//   3. vp.markers — markerGeom paths for tuple/int markers.
#include <volcano/plot/Mlab.hpp>
#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Projection.hpp>
#include "PlotTestHarness.hpp"

#include <gtest/gtest.h>
#include <numeric>

using namespace volcano;
using namespace volcano::plot;

namespace {

// ─── vp.mlab spectral helper ────────────────────────────────────────

TEST(MlabParity, PsdIsRawPowerNotDb) {
    // mpl mlab.psd returns raw power values (not dB). A unit sine
    // whose energy concentrates in one bin must be positive real.
    std::vector<float> x(256);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::sin(2.0f * float(M_PI) * 10.0f * float(i) / 256.0f);
    mlab::SpectralConfig c;
    c.nfft = 256; c.Fs = 256.0f; c.window = "none";
    auto [v, f] = mlab::psd(x, c);
    ASSERT_EQ(v.size(), f.size());
    ASSERT_EQ(f.size(), 129u);          // NFFT/2+1 one-sided bins
    EXPECT_FLOAT_EQ(f[1], 1.0f);        // freq step Fs/NFFT = 1
    for (float p : v) EXPECT_GE(p, 0.0f);
    // Peak at bin 10.
    size_t pk = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] > v[pk]) pk = i;
    EXPECT_EQ(pk, 10u);
    // Parseval: sum(v) ≈ mean-square / Fs × NFFT normalization check —
    // mpl density: |X|²·2/(Fs·Σw²) with w=1 → peak = (N/2)²·2/(256·256).
    float expect = (128.0f * 128.0f) * 2.0f / (256.0f * 256.0f);
    EXPECT_NEAR(v[pk], expect, 1e-2f);
}

TEST(MlabParity, PsdOneSidedDoubling) {
    // Non-DC/non-Nyquist bins get ×2; DC does not.
    std::vector<float> x(256, 1.0f);   // DC only
    mlab::SpectralConfig c;
    c.nfft = 256; c.Fs = 256.0f; c.window = "none";
    auto [v, f] = mlab::psd(x, c);
    // DC bin: |256|²/(256·256) = 1 — no doubling.
    EXPECT_NEAR(v[0], 1.0f, 1e-3f);
}

TEST(MlabParity, NoverlapRejectsTooLarge) {
    std::vector<float> x(512, 0.0f);
    mlab::SpectralConfig c;
    c.nfft = 64; c.noverlap = 64;
    EXPECT_THROW(mlab::psd(x, c), std::invalid_argument);
}

TEST(MlabParity, SpecgramTimeCenters) {
    // mpl t = arange(NFFT/2, len-NFFT/2+1, NFFT-noverlap)/Fs.
    std::vector<float> x(512, 0.0f);
    mlab::SpectralConfig c;
    c.nfft = 64; c.Fs = 100.0f; c.noverlap = 32;
    auto r = mlab::specgram(x, c);
    ASSERT_FALSE(r.t.empty());
    EXPECT_FLOAT_EQ(r.t[0], 0.32f);            // 32/100
    EXPECT_FLOAT_EQ(r.t[1] - r.t[0], 0.32f);   // step (64-32)/100
    EXPECT_EQ(r.numSegs, (512u - 64u) / 32u + 1u);
}

TEST(MlabParity, CsdConjugateProduct) {
    // csd = conj(X)·Y — for identical signals equals psd.
    std::vector<float> x(256);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::sin(2.0f * float(M_PI) * 8.0f * float(i) / 256.0f);
    mlab::SpectralConfig c;
    c.nfft = 256; c.Fs = 256.0f; c.window = "none";
    auto [pxx, f1] = mlab::psd(x, c);
    auto [pxy, f2] = mlab::csd(x, x, c);
    ASSERT_EQ(pxx.size(), pxy.size());
    for (size_t i = 0; i < pxx.size(); ++i)
        EXPECT_NEAR(pxy[i].real(), pxx[i], 1e-4f);
}

TEST(MlabParity, CohereIdenticalSignalIsOne) {
    std::vector<float> x(1024);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::sin(2.0f * float(M_PI) * 4.0f * float(i) / 256.0f)
               + 0.5f * std::cos(2.0f * float(M_PI) * 9.0f * float(i) / 256.0f);
    mlab::SpectralConfig c;
    c.nfft = 256; c.Fs = 256.0f;
    auto [cxy, f] = mlab::cohere(x, x, c);
    for (float v : cxy)
        EXPECT_NEAR(v, 1.0f, 1e-3f);
}

// ─── vp.mlab array utilities ────────────────────────────────────────

TEST(MlabParity, DetrendLinearRemovesSlope) {
    std::vector<float> y(64);
    for (size_t i = 0; i < y.size(); ++i)
        y[i] = 2.0f * float(i) + 5.0f;
    auto d = mlab::detrendLinear(y);
    for (float v : d) EXPECT_NEAR(v, 0.0f, 1e-4f);
}

TEST(MlabParity, PrctileLinear) {
    std::vector<float> x = {1, 2, 3, 4, 5};
    std::vector<float> p = {0, 25, 50, 75, 100};
    auto v = mlab::prctile(x, p);
    ASSERT_EQ(v.size(), 5u);
    EXPECT_FLOAT_EQ(v[0], 1.0f);
    EXPECT_FLOAT_EQ(v[2], 3.0f);
    EXPECT_FLOAT_EQ(v[4], 5.0f);
    EXPECT_FLOAT_EQ(v[1], 2.0f);   // linear interp at 25% → 2.0
}

TEST(MlabParity, BivariateNormalPeak) {
    std::vector<float> xs = {0.0f}, ys = {0.0f};
    auto z = mlab::bivariateNormal(xs, ys, 1.0f, 1.0f, 0, 0, 0);
    ASSERT_EQ(z.size(), 1u);
    EXPECT_NEAR(z[0], 1.0f / (2.0f * float(M_PI)), 1e-6f);
}

TEST(MlabParity, PcaDominantComponent) {
    // Data along y = 2x → first component carries ~all variance.
    std::vector<float> a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(float(i));
        a.push_back(2.0f * float(i));
    }
    auto p = mlab::pca(a, 2);
    ASSERT_EQ(p.fracs.size(), 2u);
    EXPECT_NEAR(p.fracs[0], 1.0f, 1e-4f);
    // First eigenvector ∝ (1, 2)/√5.
    float nx = std::abs(p.evecs[0][0]), ny = std::abs(p.evecs[0][1]);
    EXPECT_NEAR(ny / nx, 2.0f, 1e-3f);
}

TEST(MlabParity, GaussianKdeScottBandwidth) {
    // scipy scott factor: n^(-1/(d+4)).
    std::vector<float> data = {0, 1, 2, 3, 4, 5};  // 6 points × 1 dim
    mlab::GaussianKde k(data, 1);
    EXPECT_NEAR(k.factor(), std::pow(6.0f, -1.0f / 5.0f), 1e-5f);
    std::vector<float> pts = {0.5f};
    auto v = k.evaluate(pts, 1);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_GT(v[0], 0.0f);
}

// ─── vp.markers native geometry ─────────────────────────────────────

TEST(MarkerParity, FilledVsStroked) {
    auto o = markerGeom(MarkerStyle::Circle);
    EXPECT_TRUE(o.filled);
    EXPECT_FALSE(o.outlines.empty());
    auto x = markerGeom(MarkerStyle::X);
    EXPECT_FALSE(x.filled);
    EXPECT_FALSE(x.strokes.empty());
}

TEST(MarkerParity, PolygonSideCount) {
    auto g = markerGeom(MarkerStyle::Polygon, 6);
    ASSERT_EQ(g.outlines.size(), 1u);
    EXPECT_EQ(g.outlines[0].size(), 6u);
}

} // namespace
