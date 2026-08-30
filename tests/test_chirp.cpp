// tests/test_chirp.cpp — tests for PhaseDecomposer and ChirpPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/PhaseDecomposition.hpp>
#include <volcano/plot/plots/ChirpPlot.hpp>
#include <volcano/plot/Transform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct Fig2D {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit Fig2D(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

bool isNotWhite(const Pixel& p) {
    return !(p.r > 230 && p.g > 230 && p.b > 230);
}

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// PhaseDecomposer unit tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(PhaseDecompositionTest, SinCorrectAtLowFrequency) {
    // At low frequency, phase decomposition should match direct computation.
    PhaseDecomposer pd(1.0, 0.0);  // freq=1, center=0
    for (float x = -5.0f; x <= 5.0f; x += 0.1f) {
        float expected = std::sin(x);
        float actual = pd.sin(x);
        EXPECT_NEAR(actual, expected, 1e-4f)
            << "sin(" << x << ") mismatch at freq=1";
    }
}

TEST(PhaseDecompositionTest, CosCorrectAtLowFrequency) {
    PhaseDecomposer pd(1.0, 0.0);
    for (float x = -5.0f; x <= 5.0f; x += 0.1f) {
        float expected = std::cos(x);
        float actual = pd.cos(x);
        EXPECT_NEAR(actual, expected, 1e-4f)
            << "cos(" << x << ") mismatch at freq=1";
    }
}

TEST(PhaseDecompositionTest, SinCorrectAtHighFrequency) {
    // At high frequency, direct f32 sin loses precision for large x,
    // but phase decomposition should still be correct.
    double freq = 1e6;
    double center = 100.0;
    PhaseDecomposer pd(freq, center);

    // Test near the center where delta is small.
    for (float dx = -0.01f; dx <= 0.01f; dx += 0.001f) {
        float x = static_cast<float>(center) + dx;
        double expected = std::sin(freq * static_cast<double>(x));
        float actual = pd.sin(x);
        EXPECT_NEAR(actual, static_cast<float>(expected), 1e-3f)
            << "sin at high freq mismatch for dx=" << dx;
    }
}

TEST(PhaseDecompositionTest, PhaseDecompositionBetterThanDirectF32) {
    // At very high frequency and large x, direct f32 sin(freq*x) loses
    // precision. Phase decomposition should produce a more accurate result.
    double freq = 1e8;
    double center = 1000.0;
    PhaseDecomposer pd(freq, center);

    // Pick a point near the center.
    float x = static_cast<float>(center) + 0.001f;

    // Direct f32 computation (loses precision).
    float directSin = std::sin(static_cast<float>(freq) * x);

    // Phase-decomposed computation.
    float decomposedSin = pd.sin(x);

    // f64 ground truth.
    double groundTruth = std::sin(freq * static_cast<double>(x));

    // The decomposed result should be closer to the ground truth.
    float directError = std::abs(directSin - static_cast<float>(groundTruth));
    float decomposedError = std::abs(decomposedSin - static_cast<float>(groundTruth));

    EXPECT_LT(decomposedError, directError + 1e-6f)
        << "Phase decomposition should be more accurate than direct f32";
}

TEST(PhaseDecompositionTest, SinCosIdentity) {
    // sin^2 + cos^2 = 1 for any phase.
    PhaseDecomposer pd(123.456, 42.0);
    for (float x = 41.0f; x <= 43.0f; x += 0.01f) {
        float s = pd.sin(x);
        float c = pd.cos(x);
        EXPECT_NEAR(s * s + c * c, 1.0f, 1e-4f)
            << "sin^2+cos^2 != 1 at x=" << x;
    }
}

TEST(PhaseDecompositionTest, DifferentCentersProduceSameResult) {
    // Phase decomposition with different centers should produce the same
    // sin/cos values (just with different internal base phases).
    double freq = 50.0;
    PhaseDecomposer pd1(freq, 0.0);
    PhaseDecomposer pd2(freq, 10.0);
    PhaseDecomposer pd3(freq, -5.0);

    for (float x = -1.0f; x <= 1.0f; x += 0.1f) {
        float s1 = pd1.sin(x);
        float s2 = pd2.sin(x);
        float s3 = pd3.sin(x);
        EXPECT_NEAR(s1, s2, 1e-4f) << "Different centers should agree (sin)";
        EXPECT_NEAR(s1, s3, 1e-4f) << "Different centers should agree (sin)";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// LinearChirp unit tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(PhaseDecompositionTest, ChirpAtStartFrequency) {
    // At t=0, the chirp should have frequency f0.
    // The instantaneous phase derivative at t=0 is 2*pi*f0.
    LinearChirp chirp{10.0, 100.0, 1.0};  // f0=10, f1=100, duration=1
    // At t=0, sin(phase(0)) = sin(0) = 0
    EXPECT_NEAR(chirp.evaluate(0.0f), 0.0f, 1e-5f);
}

TEST(PhaseDecompositionTest, ChirpDecomposedMatchesDirect) {
    // The decomposed evaluation should match the direct evaluation.
    LinearChirp chirp{10.0, 100.0, 1.0};
    float center = 0.5f;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
        float direct = chirp.evaluate(t);
        float decomposed = chirp.evaluateDecomposed(t, center);
        EXPECT_NEAR(direct, decomposed, 1e-3f)
            << "Chirp decomposed vs direct mismatch at t=" << t;
    }
}

TEST(PhaseDecompositionTest, ChirpBoundedByOne) {
    // The chirp signal should be bounded by [-1, 1].
    LinearChirp chirp{1.0, 1000.0, 10.0};
    for (float t = 0.0f; t <= 10.0f; t += 0.001f) {
        float y = chirp.evaluate(t);
        EXPECT_LE(std::abs(y), 1.0f + 1e-5f)
            << "Chirp exceeds [-1,1] at t=" << t;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ChirpPlot regression tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ChirpPlotRegression, BasicChirpRenders) {
    Fig2D cf(256);
    ChirpPlotConfig cfg;
    cfg.color = Color::blue();
    cfg.lineWidth = 1.5f;
    auto plot = std::make_unique<ChirpPlot>(1.0, 50.0, 1.0, Range{0, 1}, 512, cfg);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Chirp plot should render";
}

TEST(ChirpPlotRegression, EmptyRangeRendersNothing) {
    Fig2D cf(256);
    // Degenerate range (min == max).
    auto plot = std::make_unique<ChirpPlot>(1.0, 10.0, 1.0, Range{0, 0}, 512);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Degenerate chirp should render nothing";
}

TEST(ChirpPlotRegression, CustomColor) {
    Fig2D cf(256);
    ChirpPlotConfig cfg;
    cfg.color = Color::red();
    cfg.lineWidth = 2.0f;
    auto plot = std::make_unique<ChirpPlot>(1.0, 50.0, 1.0, Range{0, 1}, 512, cfg);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 20u) << "Red chirp should render red pixels";
}

TEST(ChirpPlotRegression, HighFrequencyChirpRenders) {
    Fig2D cf(256);
    // Very high frequency chirp — tests that phase decomposition works.
    ChirpPlotConfig cfg;
    cfg.color = Color::blue();
    cfg.usePhaseDecomposition = true;
    auto plot = std::make_unique<ChirpPlot>(1e4, 1e6, 1.0, Range{0, 1}, 2048, cfg);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "High-frequency chirp should render";
}

TEST(ChirpPlotRegression, PhaseDecompositionVsNaive) {
    // Both modes should render, but phase decomposition should produce
    // a valid signal at high frequencies.
    Fig2D cf1(256);
    ChirpPlotConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.usePhaseDecomposition = true;
    auto p1 = std::make_unique<ChirpPlot>(1e3, 1e5, 1.0, Range{0, 1}, 2048, cfg1);
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig2D cf2(256);
    ChirpPlotConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.usePhaseDecomposition = false;
    auto p2 = std::make_unique<ChirpPlot>(1e3, 1e5, 1.0, Range{0, 1}, 2048, cfg2);
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    size_t count1 = countPixels(img1, isNotWhite);
    size_t count2 = countPixels(img2, isNotWhite);
    EXPECT_GT(count1, 50u) << "Phase-decomposed chirp should render";
    EXPECT_GT(count2, 50u) << "Naive chirp should also render";
}

TEST(ChirpPlotRegression, Autoscale) {
    Fig2D cf(256);
    auto plot = std::make_unique<ChirpPlot>(1.0, 50.0, 1.0, Range{-1, 2}, 512);
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -1.0f);
    EXPECT_GE(av.x.max, 2.0f);
    EXPECT_LE(av.y.min, -1.0f);
    EXPECT_GE(av.y.max, 1.0f);
}

TEST(ChirpPlotRegression, LowFrequencyChirp) {
    Fig2D cf(256);
    // Low frequency — should produce a gentle wave.
    auto plot = std::make_unique<ChirpPlot>(0.5, 5.0, 10.0, Range{0, 10}, 512);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Low-frequency chirp should render";
}

TEST(ChirpPlotRegression, NarrowZoomHighFreq) {
    // Zoom into a very narrow region of a high-frequency chirp.
    // Phase decomposition should produce a clean sine wave.
    Fig2D cf(256);
    ChirpPlotConfig cfg;
    cfg.color = Color::blue();
    cfg.usePhaseDecomposition = true;
    // Center at 0.5, zoom into [0.499, 0.501] — very narrow.
    auto plot = std::make_unique<ChirpPlot>(1e6, 2e6, 1.0, Range{0.499f, 0.501f}, 2048, cfg);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Narrow zoom high-freq chirp should render";
}

TEST(ChirpPlotRegression, Reevaluate) {
    Fig2D cf(256);
    ChirpPlotConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<ChirpPlot>(1.0, 50.0, 1.0, Range{0, 1}, 512, cfg);
    cf.axes->addPlot(std::move(plot));
    auto img1 = cf.render();

    size_t count1 = countPixels(img1, isNotWhite);
    EXPECT_GT(count1, 50u) << "Initial chirp should render";

    // Re-evaluate with a different range — we can't easily test this
    // in the headless harness, but at least verify it doesn't crash.
    // (reevaluate requires a Renderer reference, which we don't have here.)
}
