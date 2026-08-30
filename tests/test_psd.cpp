// tests/test_psd.cpp — tests for PsdPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/PsdPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct PsdFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit PsdFig(uint32_t size = 256)
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

TEST(PsdRegression, BasicPsdRenders) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "PSD should render";
}

TEST(PsdRegression, PsdPeakAtSignalFreq) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 32}; vp.y = {-100, 0}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    // Peak should be at 4 Hz.
    float nx = (4.0f - vp.x.min) / vp.x.span();
    uint32_t px = static_cast<uint32_t>(nx * cf.axes->rect.width);
    bool foundPeak = false;
    for (uint32_t y = 0; y < img.height(); ++y) {
        if (px < img.width() && isBlue(img.get(px, y))) foundPeak = true;
    }
    EXPECT_TRUE(foundPeak) << "PSD should have peak at signal frequency";
}

TEST(PsdRegression, EmptySignalRendersNothing) {
    PsdFig cf(256);
    std::vector<float> signal;
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty signal should render nothing";
}

TEST(PsdRegression, ConstantSignalHasDcPeak) {
    PsdFig cf(256);
    std::vector<float> signal(64, 5.0f);
    PsdConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Constant signal should have DC peak in PSD";
}

// ═══════════════════════════════════════════════════════════════════════════
// Window functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(PsdRegression, HannWindowRenders) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.window = PsdConfig::Hann;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hann window PSD should render";
}

TEST(PsdRegression, HammingWindowRenders) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.window = PsdConfig::Hamming;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hamming window PSD should render";
}

TEST(PsdRegression, BlackmanWindowRenders) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.window = PsdConfig::Blackman;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Blackman window PSD should render";
}

TEST(PsdRegression, RectangularWindowRenders) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.window = PsdConfig::Rectangular;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Rectangular window PSD should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features
// ═══════════════════════════════════════════════════════════════════════════

TEST(PsdRegression, CustomColor) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red PSD should render red pixels";
}

TEST(PsdRegression, TwoToneSignalShowsTwoPeaks) {
    PsdFig cf(256);
    auto s1 = sineWave(64, 4.0f, 64.0f, 1.0f);
    auto s2 = sineWave(64, 12.0f, 64.0f, 0.5f);
    for (int i = 0; i < 64; ++i) s1[i] += s2[i];
    PsdConfig cfg;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(s1), cfg));
    cf.render();

    Viewport vp;
    vp.x = {0, 32}; vp.y = {-100, 0}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    auto checkFreq = [&](float freq) {
        float nx = (freq - vp.x.min) / vp.x.span();
        uint32_t px = static_cast<uint32_t>(nx * cf.axes->rect.width);
        for (uint32_t y = 0; y < img.height(); ++y) {
            if (px < img.width() && isBlue(img.get(px, y))) return true;
        }
        return false;
    };
    EXPECT_TRUE(checkFreq(4.0f)) << "PSD peak at 4 Hz should exist";
    EXPECT_TRUE(checkFreq(12.0f)) << "PSD peak at 12 Hz should exist";
}

TEST(PsdRegression, CustomNfft) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.nfft = 128;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Custom nfft PSD should render";
}

TEST(PsdRegression, Autoscale) {
    PsdFig cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    PsdConfig cfg;
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<PsdPlot>(std::move(signal), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to ~31 (one-sided, halfN-1 bins).
    EXPECT_NEAR(av.x.min, -1.55f, 1.0f);
    EXPECT_NEAR(av.x.max, 32.55f, 1.0f);
    // Y: dB values, should be negative (power < 1).
    EXPECT_LT(av.y.max, 10.0f);
}
