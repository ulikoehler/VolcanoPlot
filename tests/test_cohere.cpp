// tests/test_cohere.cpp — tests for CoherePlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/CoherePlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct CohereFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit CohereFig(uint32_t size = 256)
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

bool isBlue(const Pixel& p) {
    return p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20;
}

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

std::vector<float> sineWave(int n, float freq, float sampleRate, float amp = 1.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sampleRate);
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(CohereRegression, BasicCohereRenders) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Coherence should render";
}

TEST(CohereRegression, IdenticalSignalsPerfectCoherence) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = x;  // identical
    CohereConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    auto plot = std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg);
    auto* raw = plot.get();
    cf.axes->addPlot(std::move(plot));
    cf.render();

    // For identical signals, coherence should be 1.0 at all frequencies
    // where both signals have power.
    const auto& vals = raw->values();
    const auto& freqs = raw->frequencies();
    ASSERT_FALSE(vals.empty());
    // At the signal frequency (4 Hz), coherence should be ~1.0.
    float maxCoh = 0.0f;
    for (size_t i = 0; i < vals.size(); ++i)
        maxCoh = std::max(maxCoh, vals[i]);
    EXPECT_NEAR(maxCoh, 1.0f, 0.01f) << "Identical signals should have coherence 1.0";
}

TEST(CohereRegression, EmptySignalsRendersNothing) {
    CohereFig cf(256);
    std::vector<float> x, y;
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty signals should render nothing";
}

TEST(CohereRegression, CoherenceInRange01) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CohereConfig cfg;
    cfg.sampleRate = 64.0f;
    auto plot = std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg);
    auto* raw = plot.get();
    cf.axes->addPlot(std::move(plot));
    cf.render();

    for (float v : raw->values()) {
        EXPECT_GE(v, 0.0f) << "Coherence should be >= 0";
        EXPECT_LE(v, 1.0f) << "Coherence should be <= 1";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Window functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(CohereRegression, HannWindowRenders) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.window = CohereConfig::Hann;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hann window coherence should render";
}

TEST(CohereRegression, HammingWindowRenders) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.window = CohereConfig::Hamming;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hamming window coherence should render";
}

TEST(CohereRegression, BlackmanWindowRenders) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.window = CohereConfig::Blackman;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Blackman window coherence should render";
}

TEST(CohereRegression, RectangularWindowRenders) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.window = CohereConfig::Rectangular;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Rectangular window coherence should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features
// ═══════════════════════════════════════════════════════════════════════════

TEST(CohereRegression, CustomColor) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red coherence should render red pixels";
}

TEST(CohereRegression, DifferentFreqSignals) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 12.0f, 64.0f);
    CohereConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Different freq coherence should render";
}

TEST(CohereRegression, CustomNfft) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.nfft = 128;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Custom nfft coherence should render";
}

TEST(CohereRegression, Autoscale) {
    CohereFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CohereConfig cfg;
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to ~31 (one-sided, halfN-1 bins).
    EXPECT_NEAR(av.x.min, -1.55f, 1.0f);
    EXPECT_NEAR(av.x.max, 32.55f, 1.0f);
    // Y: coherence in [0, 1], with viewport padding.
    EXPECT_GE(av.y.min, -0.1f);
    EXPECT_LE(av.y.max, 1.6f);
}

TEST(CohereRegression, PhaseShiftedSignalsCoherent) {
    CohereFig cf(256);
    // x and y are same frequency but y is phase-shifted by pi/2.
    // They should still be perfectly coherent (coherence = 1).
    int n = 64;
    float sr = 64.0f;
    float freq = 4.0f;
    std::vector<float> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sr);
        y[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sr + M_PI / 2);
    }
    CohereConfig cfg;
    cfg.sampleRate = sr;
    auto plot = std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg);
    auto* raw = plot.get();
    cf.axes->addPlot(std::move(plot));
    cf.render();

    // Phase-shifted same-frequency signals should still have coherence ~1.
    float maxCoh = 0.0f;
    for (float v : raw->values())
        maxCoh = std::max(maxCoh, v);
    EXPECT_NEAR(maxCoh, 1.0f, 0.01f)
        << "Phase-shifted same-freq signals should be coherent";
}

TEST(CohereRegression, ConstantSignalsCoherent) {
    CohereFig cf(256);
    std::vector<float> x(64, 5.0f);
    std::vector<float> y(64, 3.0f);
    CohereConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CoherePlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Constant signals coherence should render";
}
