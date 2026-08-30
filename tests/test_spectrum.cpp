// tests/test_spectrum.cpp — tests for SpectrumPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/SpectrumPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct SpecFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit SpecFigure(uint32_t size = 256)
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

/// Generate a sine wave at given frequency.
std::vector<float> sineWave(int n, float freq, float sampleRate, float amp = 1.0f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * freq * i / sampleRate);
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Magnitude spectrum
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpectrumRegression, BasicMagnitudeSpectrumRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Magnitude spectrum should render";
}

TEST(SpectrumRegression, MagnitudeSpectrumPeakAtSignalFreq) {
    SpecFigure cf(256);
    // 4 Hz signal, sample rate 64 Hz, 64 samples.
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 32}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Peak should be at 4 Hz. Check that there's a blue pixel near x=4.
    float nx = (4.0f - vp.x.min) / vp.x.span();
    uint32_t px = static_cast<uint32_t>(nx * cf.axes->rect.width);
    bool foundPeak = false;
    for (uint32_t y = 0; y < img2.height(); ++y) {
        if (px < img2.width() && isBlue(img2.get(px, y))) foundPeak = true;
    }
    EXPECT_TRUE(foundPeak) << "Magnitude spectrum should have peak at signal frequency";
}

TEST(SpectrumRegression, MagnitudeSpectrumDbScale) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.scale = SpectrumScale::dB;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "dB scale magnitude spectrum should render";
}

TEST(SpectrumRegression, MagnitudeSpectrumAutoscale) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to ~31 (one-sided spectrum, halfN-1 bins).
    // With 5% padding: 0 - 1.55 = -1.55, 31 + 1.55 = 32.55.
    EXPECT_NEAR(av.x.min, -1.55f, 1.0f);
    EXPECT_NEAR(av.x.max, 32.55f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase / Angle spectrum
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpectrumRegression, BasicPhaseSpectrumRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Phase;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Phase spectrum should render";
}

TEST(SpectrumRegression, BasicAngleSpectrumRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Angle;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Angle spectrum should render";
}

TEST(SpectrumRegression, PhaseSpectrumRangeInPi) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Phase;
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // Phase should be in [-pi, pi].
    EXPECT_GE(av.y.min, -static_cast<float>(M_PI) - 1.0f);
    EXPECT_LE(av.y.max, static_cast<float>(M_PI) + 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Window functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpectrumRegression, HannWindowRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.window = SpectrumConfig::Hann;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hann window spectrum should render";
}

TEST(SpectrumRegression, HammingWindowRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.window = SpectrumConfig::Hamming;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Hamming window spectrum should render";
}

TEST(SpectrumRegression, BlackmanWindowRenders) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.window = SpectrumConfig::Blackman;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Blackman window spectrum should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpectrumRegression, EmptySignalRendersNothing) {
    SpecFigure cf(256);
    std::vector<float> signal;
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty signal should render nothing";
}

TEST(SpectrumRegression, ConstantSignalRenders) {
    SpecFigure cf(256);
    std::vector<float> signal(64, 5.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    // DC component should have a peak at freq=0.
    EXPECT_GT(filledCount, 10u) << "Constant signal should have DC peak";
}

TEST(SpectrumRegression, CustomColor) {
    SpecFigure cf(256);
    auto signal = sineWave(64, 4.0f, 64.0f);
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(signal), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red spectrum should render red pixels";
}

TEST(SpectrumRegression, TwoToneSignalShowsTwoPeaks) {
    SpecFigure cf(256);
    // Signal with two frequency components: 4 Hz and 12 Hz.
    auto s1 = sineWave(64, 4.0f, 64.0f, 1.0f);
    auto s2 = sineWave(64, 12.0f, 64.0f, 0.5f);
    for (int i = 0; i < 64; ++i) s1[i] += s2[i];
    SpectrumConfig cfg;
    cfg.type = SpectrumType::Magnitude;
    cfg.color = Color::blue();
    cfg.sampleRate = 64.0f;
    cf.axes->addPlot(std::make_unique<SpectrumPlot>(std::move(s1), cfg));
    auto img = cf.render();

    Viewport vp;
    vp.x = {0, 32}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Check peaks at 4 Hz and 12 Hz.
    auto checkFreq = [&](float freq) {
        float nx = (freq - vp.x.min) / vp.x.span();
        uint32_t px = static_cast<uint32_t>(nx * cf.axes->rect.width);
        for (uint32_t y = 0; y < img2.height(); ++y) {
            if (px < img2.width() && isBlue(img2.get(px, y))) return true;
        }
        return false;
    };
    EXPECT_TRUE(checkFreq(4.0f)) << "Peak at 4 Hz should exist";
    EXPECT_TRUE(checkFreq(12.0f)) << "Peak at 12 Hz should exist";
}
