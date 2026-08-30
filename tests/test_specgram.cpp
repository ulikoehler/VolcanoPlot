// tests/test_specgram.cpp — tests for SpecgramPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/SpecgramPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct SpecFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit SpecFig(uint32_t size = 256)
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

// Generate a chirp signal: frequency sweeps from f0 to f1.
std::vector<float> chirp(int n, float f0, float f1, float sampleRate) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float freq = f0 + (f1 - f0) * t / (n / sampleRate);
        s[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq * t);
    }
    return s;
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

TEST(SpecgramRegression, BasicSpecgramRenders) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Specgram should fill area with colors";
}

TEST(SpecgramRegression, EmptySignalRendersNothing) {
    SpecFig cf(256);
    std::vector<float> signal;
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty signal should render nothing";
}

TEST(SpecgramRegression, ShortSignalRendersNothing) {
    SpecFig cf(256);
    std::vector<float> signal(10, 1.0f);  // shorter than nfft
    SpecgramConfig cfg;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Signal shorter than nfft should render nothing";
}

TEST(SpecgramRegression, Autoscale) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to ncols * timeStep (with padding).
    EXPECT_NEAR(av.x.min, -0.1f, 0.5f);
    EXPECT_GT(av.x.max, 1.0f);
    // Y: 0 to nrows * freqStep = 32 * (100/64) = 50 Hz.
    EXPECT_NEAR(av.y.min, -2.5f, 2.0f);
    EXPECT_NEAR(av.y.max, 52.5f, 2.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Window functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpecgramRegression, HannWindowRenders) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.window = SpecgramConfig::Hann;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Hann window specgram should render";
}

TEST(SpecgramRegression, HammingWindowRenders) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.window = SpecgramConfig::Hamming;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Hamming window specgram should render";
}

TEST(SpecgramRegression, BlackmanWindowRenders) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.window = SpecgramConfig::Blackman;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Blackman window specgram should render";
}

TEST(SpecgramRegression, RectangularWindowRenders) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.window = SpecgramConfig::Rectangular;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Rectangular window specgram should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpecgramRegression, CustomColormap) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Custom colormap specgram should render";
}

TEST(SpecgramRegression, DifferentColormapsProduceDifferentColors) {
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfgBase;
    cfgBase.sampleRate = 100.0f;
    cfgBase.nfft = 64;
    cfgBase.noverlap = 32;

    SpecFig cf1(256);
    auto s1 = signal;
    SpecgramConfig cfg1 = cfgBase;
    cfg1.cmap = &colormaps::viridis();
    cf1.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(s1), cfg1));
    auto imgViridis = cf1.render();

    SpecFig cf2(256);
    auto s2 = signal;
    SpecgramConfig cfg2 = cfgBase;
    cfg2.cmap = &colormaps::plasma();
    cf2.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(s2), cfg2));
    auto imgPlasma = cf2.render();

    Pixel p1 = imgViridis.get(128, 128);
    Pixel p2 = imgPlasma.get(128, 128);
    EXPECT_FALSE(p1.approx(p2, 30))
        << "Different colormaps should produce different colors";
}

TEST(SpecgramRegression, ExplicitValueRange) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {-80, 0};
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Explicit value range specgram should render";
}

TEST(SpecgramRegression, ChirpSignalRenders) {
    SpecFig cf(256);
    auto signal = chirp(512, 5.0f, 40.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Chirp specgram should render";
}

TEST(SpecgramRegression, NoOverlap) {
    SpecFig cf(256);
    auto signal = sineWave(512, 10.0f, 100.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 0;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "No-overlap specgram should render";
}

TEST(SpecgramRegression, ConstantSignal) {
    SpecFig cf(256);
    std::vector<float> signal(512, 1.0f);
    SpecgramConfig cfg;
    cfg.sampleRate = 100.0f;
    cfg.nfft = 64;
    cfg.noverlap = 32;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<SpecgramPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Constant signal specgram should render";
}
