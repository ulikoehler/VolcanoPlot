// tests/test_stackplot.cpp — tests for StackPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/StackPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct StackFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit StackFigure(uint32_t size = 256)
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

TEST(StackRegression, StackplotRendersTwoSeries) {
    // Two series stacked: y1 = [1, 2, 3], y2 = [2, 1, 2].
    // Total: [3, 3, 5].
    StackFigure cf(256);
    cf.axes->addPlot(std::make_unique<StackPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<std::vector<float>>{
            {1, 2, 3},
            {2, 1, 2},
        }));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Stackplot should fill area";
}

TEST(StackRegression, StackplotAutoscaleMatchesTotal) {
    StackFigure cf(256);
    cf.axes->addPlot(std::make_unique<StackPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<std::vector<float>>{
            {1, 2, 3},
            {2, 1, 2},
        }));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    // Y max = 5 (total), Y min = 0 (baseline).
    EXPECT_NEAR(av.y.min, -0.25f, 0.1f);
    EXPECT_NEAR(av.y.max, 5.25f, 0.1f);
}

TEST(StackRegression, StackplotThreeSeriesRenders) {
    StackFigure cf(256);
    cf.axes->addPlot(std::make_unique<StackPlot>(
        std::vector<float>{0, 1, 2, 3, 4},
        std::vector<std::vector<float>>{
            {1, 1, 1, 1, 1},
            {2, 2, 2, 2, 2},
            {1, 3, 1, 3, 1},
        }));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 2000u) << "Three series stackplot should fill area";
}

TEST(StackRegression, StackplotCustomColors) {
    StackFigure cf(256);
    cf.axes->addPlot(std::make_unique<StackPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<std::vector<float>>{
            {1, 2, 3},
            {2, 1, 2},
        },
        std::vector<Color>{
            Color::fromRgba8(255, 0, 0, 200),
            Color::fromRgba8(0, 0, 255, 200),
        }));
    auto img = cf.render();

    // Should have both red and blue pixels.
    size_t redCount = 0, blueCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
            if (p.b > 150 && p.b > p.r + 30 && p.b > p.g + 30) ++blueCount;
        }
    EXPECT_GT(redCount, 100u) << "Should have red series";
    EXPECT_GT(blueCount, 100u) << "Should have blue series";
}
