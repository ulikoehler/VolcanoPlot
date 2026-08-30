// tests/test_csd.cpp — tests for CsdPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/CsdPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct CsdFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit CsdFig(uint32_t size = 256)
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

TEST(CsdRegression, BasicCsdRenders) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CsdConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "CSD should render";
}

TEST(CsdRegression, IdenticalSignalsHighCsd) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = x;  // identical
    CsdConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // For identical signals, CSD = PSD, so should have a strong peak at 4 Hz.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "CSD of identical signals should render";
}

TEST(CsdRegression, EmptySignalsRendersNothing) {
    CsdFig cf(256);
    std::vector<float> x, y;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty signals should render nothing";
}

TEST(CsdRegression, ConstantSignalsHaveDcPeak) {
    CsdFig cf(256);
    std::vector<float> x(64, 5.0f);
    std::vector<float> y(64, 3.0f);
    CsdConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Constant signals should have DC peak in CSD";
}

// ═══════════════════════════════════════════════════════════════════════════
// Window functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(CsdRegression, HannWindowRenders) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CsdConfig cfg;
    cfg.window = CsdConfig::Hann;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hann window CSD should render";
}

TEST(CsdRegression, HammingWindowRenders) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CsdConfig cfg;
    cfg.window = CsdConfig::Hamming;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hamming window CSD should render";
}

TEST(CsdRegression, BlackmanWindowRenders) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CsdConfig cfg;
    cfg.window = CsdConfig::Blackman;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Blackman window CSD should render";
}

TEST(CsdRegression, RectangularWindowRenders) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CsdConfig cfg;
    cfg.window = CsdConfig::Rectangular;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Rectangular window CSD should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features
// ═══════════════════════════════════════════════════════════════════════════

TEST(CsdRegression, CustomColor) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CsdConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red CSD should render red pixels";
}

TEST(CsdRegression, DifferentFreqSignals) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 12.0f, 64.0f);
    CsdConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "CSD of different freq signals should render";
}

TEST(CsdRegression, CustomNfft) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 8.0f, 64.0f);
    CsdConfig cfg;
    cfg.nfft = 128;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Custom nfft CSD should render";
}

TEST(CsdRegression, Autoscale) {
    CsdFig cf(256);
    auto x = sineWave(64, 4.0f, 64.0f);
    auto y = sineWave(64, 4.0f, 64.0f, 0.5f);
    CsdConfig cfg;
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to ~31 (one-sided, halfN-1 bins).
    EXPECT_NEAR(av.x.min, -1.55f, 1.0f);
    EXPECT_NEAR(av.x.max, 32.55f, 1.0f);
    // Y: dB values, should be negative (power < 1).
    EXPECT_LT(av.y.max, 10.0f);
}

TEST(CsdRegression, PhaseShiftedSignals) {
    CsdFig cf(256);
    // x and y are same frequency but y is phase-shifted by pi/2.
    int n = 64;
    float sr = 64.0f;
    float freq = 4.0f;
    std::vector<float> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sr);
        y[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sr + M_PI / 2);
    }
    CsdConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = sr;
    cf.axes->addPlot(std::make_unique<CsdPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Phase-shifted CSD should render";
}
