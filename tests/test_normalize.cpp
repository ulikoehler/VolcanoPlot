// tests/test_normalize.cpp — tests for matplotlib-style normalizations
#include <gtest/gtest.h>
#include <volcano/plot/Normalize.hpp>
#include <volcano/plot/Colormap.hpp>

#include <cmath>
#include <vector>

using namespace volcano::plot;

// ─── Helper ───────────────────────────────────────────────────────────────
static constexpr float kTol = 0.02f;

// ─── NormalizeLinear ──────────────────────────────────────────────────────

TEST(NormalizeLinear, MapsVminToZero) {
    NormalizeLinear n(0.0f, 10.0f);
    EXPECT_NEAR(n(0.0f), 0.0f, kTol);
}

TEST(NormalizeLinear, MapsVmaxToOne) {
    NormalizeLinear n(0.0f, 10.0f);
    EXPECT_NEAR(n(10.0f), 1.0f, kTol);
}

TEST(NormalizeLinear, MapsMidpoint) {
    NormalizeLinear n(0.0f, 10.0f);
    EXPECT_NEAR(n(5.0f), 0.5f, kTol);
}

TEST(NormalizeLinear, ClipsOutOfRange) {
    NormalizeLinear n(0.0f, 10.0f);
    EXPECT_NEAR(n(-5.0f), 0.0f, kTol);
    EXPECT_NEAR(n(15.0f), 1.0f, kTol);
}

TEST(NormalizeLinear, NoClipWhenDisabled) {
    NormalizeLinear n(0.0f, 10.0f);
    n.setClip(false);
    EXPECT_NEAR(n(-5.0f), -0.5f, kTol);
    EXPECT_NEAR(n(15.0f), 1.5f, kTol);
}

TEST(NormalizeLinear, InverseRoundTrip) {
    NormalizeLinear n(2.0f, 8.0f);
    for (float v = 2.0f; v <= 8.0f; v += 0.5f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, kTol);
    }
}

TEST(NormalizeLinear, AutoscaleFromData) {
    NormalizeLinear n;
    std::vector<float> data = {3, 1, 4, 1, 5, 9, 2, 6};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), 1.0f, kTol);
    EXPECT_NEAR(n.vmax(), 9.0f, kTol);
    EXPECT_NEAR(n(5.0f), 0.5f, kTol);
}

TEST(NormalizeLinear, AutoscalePreservesExplicitRange) {
    NormalizeLinear n(0.0f, 100.0f);
    std::vector<float> data = {3, 1, 4, 1, 5};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), 0.0f, kTol);
    EXPECT_NEAR(n.vmax(), 100.0f, kTol);
}

TEST(NormalizeLinear, AutoscaleIgnoresNaN) {
    NormalizeLinear n;
    std::vector<float> data = {1, 2, std::nanf(""), 3, 4};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), 1.0f, kTol);
    EXPECT_NEAR(n.vmax(), 4.0f, kTol);
}

// ─── NoNorm ───────────────────────────────────────────────────────────────

TEST(NoNorm, PassThrough) {
    NoNorm n;
    EXPECT_NEAR(n(0.3f), 0.3f, kTol);
    EXPECT_NEAR(n(0.7f), 0.7f, kTol);
}

TEST(NoNorm, InverseIsIdentity) {
    NoNorm n;
    EXPECT_NEAR(n.inverse(0.5f), 0.5f, kTol);
}

// ─── LogNorm ──────────────────────────────────────────────────────────────

TEST(LogNorm, MapsVminToZero) {
    LogNorm n(1.0f, 1000.0f);
    EXPECT_NEAR(n(1.0f), 0.0f, kTol);
}

TEST(LogNorm, MapsVmaxToOne) {
    LogNorm n(1.0f, 1000.0f);
    EXPECT_NEAR(n(1000.0f), 1.0f, kTol);
}

TEST(LogNorm, MapsLogMidpoint) {
    // log10(1)=0, log10(1000)=3, midpoint at log=1.5 → 10^1.5 ≈ 31.6
    LogNorm n(1.0f, 1000.0f);
    EXPECT_NEAR(n(31.6227766f), 0.5f, kTol);
}

TEST(LogNorm, ClipsNonPositive) {
    LogNorm n(1.0f, 1000.0f);
    EXPECT_NEAR(n(0.0f), 0.0f, kTol);
    EXPECT_NEAR(n(-5.0f), 0.0f, kTol);
}

TEST(LogNorm, InverseRoundTrip) {
    LogNorm n(1.0f, 1000.0f);
    for (float v = 1.0f; v <= 1000.0f; v *= 3.0f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, v * kTol);
    }
}

TEST(LogNorm, AutoscaleFromData) {
    LogNorm n;
    std::vector<float> data = {1, 10, 100, 1000};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), 1.0f, kTol);
    EXPECT_NEAR(n.vmax(), 1000.0f, kTol);
    EXPECT_NEAR(n(10.0f), 1.0f / 3.0f, kTol);
}

// ─── PowerNorm ────────────────────────────────────────────────────────────

TEST(PowerNorm, Gamma1IsLinear) {
    PowerNorm n(1.0f, 0.0f, 10.0f);
    EXPECT_NEAR(n(5.0f), 0.5f, kTol);
}

TEST(PowerNorm, Gamma2CompressesLow) {
    // With gamma=2 and positive range, low values are compressed.
    PowerNorm n(2.0f, 1.0f, 100.0f);
    // v=10: log(10/1)/log(100/1) = 0.5, then ^2 = 0.25
    EXPECT_NEAR(n(10.0f), 0.25f, kTol);
}

TEST(PowerNorm, MapsEndpoints) {
    PowerNorm n(2.0f, 1.0f, 100.0f);
    EXPECT_NEAR(n(1.0f), 0.0f, kTol);
    EXPECT_NEAR(n(100.0f), 1.0f, kTol);
}

TEST(PowerNorm, InverseRoundTrip) {
    PowerNorm n(2.0f, 1.0f, 100.0f);
    for (float v = 1.0f; v <= 100.0f; v *= 2.0f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, v * kTol);
    }
}

TEST(PowerNorm, StraddlesZero) {
    PowerNorm n(0.5f, -10.0f, 10.0f);
    EXPECT_NEAR(n(-10.0f), 0.0f, kTol);
    EXPECT_NEAR(n(10.0f), 1.0f, kTol);
    // Midpoint at 0: (0-(-10))/20 = 0.5, ^(1/0.5)=^2 = 0.25
    EXPECT_NEAR(n(0.0f), 0.25f, kTol);
}

// ─── SymLogNorm ───────────────────────────────────────────────────────────

TEST(SymLogNorm, LinearNearZero) {
    SymLogNorm n(1.0f, 1.0f, -10.0f, 10.0f);
    // v=0 should map to the midpoint (since range is symmetric).
    EXPECT_NEAR(n(0.0f), 0.5f, kTol);
}

TEST(SymLogNorm, MapsEndpoints) {
    SymLogNorm n(1.0f, 1.0f, -100.0f, 100.0f);
    EXPECT_NEAR(n(-100.0f), 0.0f, kTol);
    EXPECT_NEAR(n(100.0f), 1.0f, kTol);
}

TEST(SymLogNorm, SymmetricAroundZero) {
    SymLogNorm n(1.0f, 1.0f, -100.0f, 100.0f);
    float tPos = n(50.0f);
    float tNeg = n(-50.0f);
    EXPECT_NEAR(tPos, 1.0f - tNeg, kTol);
}

TEST(SymLogNorm, InverseRoundTrip) {
    SymLogNorm n(1.0f, 1.0f, -100.0f, 100.0f);
    for (float v = -100.0f; v <= 100.0f; v += 20.0f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, std::abs(v) * kTol + 0.1f)
            << "v=" << v;
    }
}

TEST(SymLogNorm, AutoscaleFromData) {
    SymLogNorm n(1.0f, 1.0f);
    std::vector<float> data = {-50, -1, 0, 1, 50};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), -50.0f, kTol);
    EXPECT_NEAR(n.vmax(), 50.0f, kTol);
}

// ─── AsinhNorm ────────────────────────────────────────────────────────────

TEST(AsinhNorm, MapsEndpoints) {
    AsinhNorm n(1.0f, -10.0f, 10.0f);
    EXPECT_NEAR(n(-10.0f), 0.0f, kTol);
    EXPECT_NEAR(n(10.0f), 1.0f, kTol);
}

TEST(AsinhNorm, MapsZeroToMidpoint) {
    AsinhNorm n(1.0f, -10.0f, 10.0f);
    // asinh is symmetric, so 0 maps to 0.5 for symmetric range.
    EXPECT_NEAR(n(0.0f), 0.5f, kTol);
}

TEST(AsinhNorm, InverseRoundTrip) {
    AsinhNorm n(1.0f, -10.0f, 10.0f);
    for (float v = -10.0f; v <= 10.0f; v += 2.0f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, std::abs(v) * kTol + 0.1f);
    }
}

TEST(AsinhNorm, HandlesZeroCrossing) {
    AsinhNorm n(0.5f, -1e6f, 1e6f);
    // Should not produce NaN or inf.
    float t = n(0.0f);
    EXPECT_FALSE(std::isnan(t));
    EXPECT_FALSE(std::isinf(t));
    EXPECT_NEAR(t, 0.5f, 0.01f);
}

// ─── BoundaryNorm ─────────────────────────────────────────────────────────

TEST(BoundaryNorm, MapsToBinCenters) {
    // 3 bins: [0,1), [1,2), [2,3]
    BoundaryNorm n({0, 1, 2, 3});
    // Bin 0: t = 0.5/3 ≈ 0.167
    EXPECT_NEAR(n(0.5f), 0.5f / 3.0f, kTol);
    // Bin 1: t = 1.5/3 = 0.5
    EXPECT_NEAR(n(1.5f), 1.5f / 3.0f, kTol);
    // Bin 2: t = 2.5/3 ≈ 0.833
    EXPECT_NEAR(n(2.5f), 2.5f / 3.0f, kTol);
}

TEST(BoundaryNorm, ClipsOutOfRange) {
    BoundaryNorm n({0, 1, 2, 3});
    EXPECT_NEAR(n(-1.0f), 0.0f, kTol);
    EXPECT_NEAR(n(5.0f), 1.0f, kTol);
}

TEST(BoundaryNorm, BoundaryValueGoesToUpperBin) {
    // v=1 should go to bin 1 (matplotlib convention: lower-inclusive).
    BoundaryNorm n({0, 1, 2, 3});
    EXPECT_NEAR(n(1.0f), 1.5f / 3.0f, kTol);
}

TEST(BoundaryNorm, NumBins) {
    BoundaryNorm n({0, 1, 2, 3, 4, 5});
    EXPECT_EQ(n.numBins(), 5u);
}

// ─── CenteredNorm ─────────────────────────────────────────────────────────

TEST(CenteredNorm, MapsCenterToMidpoint) {
    CenteredNorm n(0.0f, 10.0f);
    EXPECT_NEAR(n(0.0f), 0.5f, kTol);
}

TEST(CenteredNorm, MapsEndpoints) {
    CenteredNorm n(0.0f, 10.0f);
    EXPECT_NEAR(n(-10.0f), 0.0f, kTol);
    EXPECT_NEAR(n(10.0f), 1.0f, kTol);
}

TEST(CenteredNorm, AutoscaleFromData) {
    CenteredNorm n(0.0f);  // vrange unset
    std::vector<float> data = {-3, -1, 0, 2, 5};
    n.autoscale(data);
    // max(|v - 0|) = 5
    EXPECT_NEAR(n.vrange(), 5.0f, kTol);
    EXPECT_NEAR(n(-5.0f), 0.0f, kTol);
    EXPECT_NEAR(n(5.0f), 1.0f, kTol);
}

TEST(CenteredNorm, NonZeroCenter) {
    CenteredNorm n(5.0f, 3.0f);
    EXPECT_NEAR(n(2.0f), 0.0f, kTol);
    EXPECT_NEAR(n(8.0f), 1.0f, kTol);
    EXPECT_NEAR(n(5.0f), 0.5f, kTol);
}

// ─── TwoSlopeNorm ─────────────────────────────────────────────────────────

TEST(TwoSlopeNorm, MapsVcenterToHalf) {
    TwoSlopeNorm n(0.0f, -10.0f, 10.0f);
    EXPECT_NEAR(n(0.0f), 0.5f, kTol);
}

TEST(TwoSlopeNorm, MapsEndpoints) {
    TwoSlopeNorm n(0.0f, -10.0f, 10.0f);
    EXPECT_NEAR(n(-10.0f), 0.0f, kTol);
    EXPECT_NEAR(n(10.0f), 1.0f, kTol);
}

TEST(TwoSlopeNorm, PiecewiseLinear) {
    TwoSlopeNorm n(0.0f, -10.0f, 10.0f);
    // v=-5: (-5-(-10))/(0-(-10)) * 0.5 = 0.25
    EXPECT_NEAR(n(-5.0f), 0.25f, kTol);
    // v=5: 0.5 + (5-0)/(10-0) * 0.5 = 0.75
    EXPECT_NEAR(n(5.0f), 0.75f, kTol);
}

TEST(TwoSlopeNorm, AsymmetricSlopes) {
    TwoSlopeNorm n(0.0f, -2.0f, 8.0f);
    // v=-1: (-1-(-2))/(0-(-2)) * 0.5 = 0.25
    EXPECT_NEAR(n(-1.0f), 0.25f, kTol);
    // v=4: 0.5 + (4-0)/(8-0) * 0.5 = 0.75
    EXPECT_NEAR(n(4.0f), 0.75f, kTol);
}

TEST(TwoSlopeNorm, InverseRoundTrip) {
    TwoSlopeNorm n(0.0f, -10.0f, 10.0f);
    for (float v = -10.0f; v <= 10.0f; v += 2.0f) {
        float t = n(v);
        EXPECT_NEAR(n.inverse(t), v, kTol);
    }
}

TEST(TwoSlopeNorm, AutoscaleFromData) {
    TwoSlopeNorm n(0.0f);
    std::vector<float> data = {-5, -1, 0, 3, 7};
    n.autoscale(data);
    EXPECT_NEAR(n.vmin(), -5.0f, kTol);
    EXPECT_NEAR(n.vmax(), 7.0f, kTol);
}

// ─── FuncNorm ─────────────────────────────────────────────────────────────

TEST(FuncNorm, CustomMapping) {
    // Square root mapping.
    FuncNorm n(
        [](float v, float vmin, float vmax) {
            return (std::sqrt(v) - std::sqrt(vmin)) / (std::sqrt(vmax) - std::sqrt(vmin));
        },
        [](float t, float vmin, float vmax) {
            float s = std::sqrt(vmin) + t * (std::sqrt(vmax) - std::sqrt(vmin));
            return s * s;
        },
        0.0f, 100.0f);
    // v=25: sqrt(25)=5, sqrt(0)=0, sqrt(100)=10 → 5/10 = 0.5
    EXPECT_NEAR(n(25.0f), 0.5f, kTol);
}

TEST(FuncNorm, InverseRoundTrip) {
    FuncNorm n(
        [](float v, float vmin, float vmax) {
            return (v - vmin) / (vmax - vmin);
        },
        [](float t, float vmin, float vmax) {
            return vmin + t * (vmax - vmin);
        },
        0.0f, 10.0f);
    for (float v = 0; v <= 10; v += 1) {
        EXPECT_NEAR(n.inverse(n(v)), v, kTol);
    }
}

// ─── MultiNorm ────────────────────────────────────────────────────────────

TEST(MultiNorm, ChainsTwoNorms) {
    // First: linear [0, 100] → [0, 1]
    // Second: linear [0, 1] → [0, 1] (identity, but could be any norm)
    auto n1 = std::make_shared<NormalizeLinear>(0.0f, 100.0f);
    auto n2 = std::make_shared<NormalizeLinear>(0.0f, 1.0f);
    MultiNorm mn({n1, n2});
    // v=50 → n1: 0.5 → n2: 0.5
    EXPECT_NEAR(mn(50.0f), 0.5f, kTol);
}

TEST(MultiNorm, LogThenPower) {
    // First: log [1, 1000] → [0, 1]
    // Second: power(gamma=2) [0, 1] → [0, 1]
    auto n1 = std::make_shared<LogNorm>(1.0f, 1000.0f);
    auto n2 = std::make_shared<PowerNorm>(2.0f, 0.0f, 1.0f);
    MultiNorm mn({n1, n2});
    // v=100: log10(100)=2, (2-0)/3 = 0.667
    // PowerNorm with vmin=0 uses pow(ratio, 1/gamma) = pow(0.667, 0.5) = 0.817
    EXPECT_NEAR(mn(100.0f), 0.817f, 0.05f);
}

TEST(MultiNorm, InverseReversesOrder) {
    auto n1 = std::make_shared<NormalizeLinear>(0.0f, 100.0f);
    auto n2 = std::make_shared<NormalizeLinear>(0.0f, 1.0f);
    MultiNorm mn({n1, n2});
    // t=0.5 → n2.inverse: 0.5 → n1.inverse: 50
    EXPECT_NEAR(mn.inverse(0.5f), 50.0f, kTol);
}

TEST(MultiNorm, Autoscale) {
    auto n1 = std::make_shared<NormalizeLinear>();
    auto n2 = std::make_shared<NormalizeLinear>(0.0f, 1.0f);
    MultiNorm mn({n1, n2});
    std::vector<float> data = {0, 25, 50, 75, 100};
    mn.autoscale(data);
    EXPECT_NEAR(mn(50.0f), 0.5f, kTol);
}

// ─── Factory functions ────────────────────────────────────────────────────

TEST(NormsFactory, LinearFactory) {
    auto n = norms::linear(0.0f, 10.0f);
    EXPECT_NEAR((*n)(5.0f), 0.5f, kTol);
}

TEST(NormsFactory, LogFactory) {
    auto n = norms::log(1.0f, 100.0f);
    EXPECT_NEAR((*n)(10.0f), 0.5f, kTol);
}

TEST(NormsFactory, PowerFactory) {
    auto n = norms::power(1.0f, 0.0f, 10.0f);
    EXPECT_NEAR((*n)(5.0f), 0.5f, kTol);
}

TEST(NormsFactory, SymlogFactory) {
    auto n = norms::symlog(1.0f, 1.0f, -100.0f, 100.0f);
    EXPECT_NEAR((*n)(100.0f), 1.0f, kTol);
}

TEST(NormsFactory, AsinhFactory) {
    auto n = norms::asinh(1.0f, -10.0f, 10.0f);
    EXPECT_NEAR((*n)(10.0f), 1.0f, kTol);
}

TEST(NormsFactory, BoundaryFactory) {
    auto n = norms::boundary({0, 1, 2, 3});
    EXPECT_NEAR((*n)(0.5f), 0.5f / 3.0f, kTol);
}

TEST(NormsFactory, CenteredFactory) {
    auto n = norms::centered(0.0f, 10.0f);
    EXPECT_NEAR((*n)(0.0f), 0.5f, kTol);
}

TEST(NormsFactory, TwoslopeFactory) {
    auto n = norms::twoslope(0.0f, -10.0f, 10.0f);
    EXPECT_NEAR((*n)(0.0f), 0.5f, kTol);
}

// ─── Integration with Colormap ────────────────────────────────────────────

TEST(NormColormapIntegration, LogNormProducesDifferentColors) {
    LogNorm norm(1.0f, 1000.0f);
    const auto& cmap = colormaps::viridis();
    // v=1 → t=0 → first color
    Color c0 = cmap.sample(norm(1.0f));
    // v=1000 → t=1 → last color
    Color c1 = cmap.sample(norm(1000.0f));
    // They should be different.
    EXPECT_GT(std::abs(c0.r - c1.r) + std::abs(c0.g - c1.g) + std::abs(c0.b - c1.b), 0.1f);
}

TEST(NormColormapIntegration, BoundaryNormProducesDiscreteColors) {
    BoundaryNorm norm({0, 1, 2, 3, 4});
    const auto& cmap = colormaps::viridis();
    // Each bin should produce a distinct color.
    Color c0 = cmap.sample(norm(0.5f));
    Color c1 = cmap.sample(norm(1.5f));
    Color c2 = cmap.sample(norm(2.5f));
    Color c3 = cmap.sample(norm(3.5f));
    // All 4 colors should be distinct.
    auto dist = [](Color a, Color b) {
        return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
    };
    EXPECT_GT(dist(c0, c1), 0.05f);
    EXPECT_GT(dist(c1, c2), 0.05f);
    EXPECT_GT(dist(c2, c3), 0.05f);
}
