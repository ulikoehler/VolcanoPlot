// tests/test_violinplot.cpp — tests for ViolinPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ViolinPlot.hpp>

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct ViolinFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit ViolinFigure(uint32_t size = 256)
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

/// Generate a normal distribution sample.
std::vector<float> normalSample(int n, float mean, float sigma, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(mean, sigma);
    std::vector<float> data(n);
    for (int i = 0; i < n; ++i) data[i] = dist(rng);
    return data;
}

} // namespace

TEST(ViolinRegression, SingleViolinRenders) {
    ViolinFigure cf(256);
    auto data = normalSample(200, 5.0f, 1.5f);
    cf.axes->addPlot(std::make_unique<ViolinPlot>(std::move(data)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Violin should render filled body";
}

TEST(ViolinRegression, MultipleViolinsRender) {
    ViolinFigure cf(256);
    std::vector<std::vector<float>> groups = {
        normalSample(150, 3.0f, 1.0f, 42),
        normalSample(150, 5.0f, 1.5f, 123),
        normalSample(150, 7.0f, 0.8f, 456),
    };
    cf.axes->addPlot(std::make_unique<ViolinPlot>(std::move(groups)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Multiple violins should render";
}

TEST(ViolinRegression, ViolinAutoscaleMatchesData) {
    ViolinFigure cf(256);
    auto data = normalSample(200, 5.0f, 1.0f, 42);
    // Data range is roughly [2, 8].
    cf.axes->addPlot(std::make_unique<ViolinPlot>(std::move(data)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: group at x=1, width 0.5 (matplotlib default), so [0.75, 1.25] with 5% padding.
    // span=0.5, pad=0.025, so [0.725, 1.275].
    EXPECT_NEAR(av.x.min, 0.725f, 0.1f);
    EXPECT_NEAR(av.x.max, 1.275f, 0.1f);
    // Y: data range ~[2, 8] with 5% padding.
    EXPECT_LT(av.y.min, 3.0f);
    EXPECT_GT(av.y.max, 7.0f);
}

TEST(ViolinRegression, ViolinBodyIsSymmetric) {
    // The violin body should be roughly symmetric around the center x.
    ViolinFigure cf(256);
    auto data = normalSample(300, 5.0f, 1.0f, 42);
    ViolinConfig cfg;
    cfg.showBox = false;
    cf.axes->addPlot(std::make_unique<ViolinPlot>(std::move(data), cfg));
    auto img = cf.render();

    // Count non-white pixels on left vs right of center.
    // Center x=1 in data space → pixel center.
    auto vp = cf.axes->viewport();
    float nx = (1.0f - vp.x.min) / vp.x.span();
    uint32_t centerX = static_cast<uint32_t>(cf.axes->rect.x + nx * cf.axes->rect.width);

    size_t leftCount = 0, rightCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y) {
        for (uint32_t x = 0; x < centerX && x < img.width(); ++x)
            if (isNotWhite(img.get(x, y))) ++leftCount;
        for (uint32_t x = centerX; x < img.width(); ++x)
            if (isNotWhite(img.get(x, y))) ++rightCount;
    }
    // Should be roughly symmetric (within 20%).
    size_t total = leftCount + rightCount;
    if (total > 0) {
        float ratio = float(leftCount) / float(total);
        EXPECT_GT(ratio, 0.35f) << "Left side should have ~half the pixels";
        EXPECT_LT(ratio, 0.65f) << "Right side should have ~half the pixels";
    }
}

TEST(ViolinRegression, ViolinWithBoxRenders) {
    ViolinFigure cf(256);
    auto data = normalSample(200, 5.0f, 1.5f, 42);
    ViolinConfig cfg;
    cfg.showBox = true;
    cf.axes->addPlot(std::make_unique<ViolinPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Violin with box should render";
}
