// tests/test_quiver.cpp — tests for QuiverPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/QuiverPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct QuiverFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit QuiverFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

bool isBlack(const Pixel& p) {
    return p.r < 50 && p.g < 50 && p.b < 50;
}

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

TEST(QuiverRegression, SingleArrowRenders) {
    // One arrow from (1,1) with direction (1,1).
    QuiverFigure cf(256);
    cf.axes->addPlot(std::make_unique<QuiverPlot>(
        std::vector<float>{1.0f},
        std::vector<float>{1.0f},
        std::vector<float>{1.0f},
        std::vector<float>{1.0f},
        QuiverConfig{}));
    auto img = cf.render();

    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 5u) << "Single arrow should render black pixels";
}

TEST(QuiverRegression, GridOfArrowsRenders) {
    // 3x3 grid of arrows.
    QuiverFigure cf(256);
    std::vector<float> x, y, u, v;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            x.push_back(i * 2.0f + 1.0f);
            y.push_back(j * 2.0f + 1.0f);
            u.push_back(1.0f);
            v.push_back(0.5f);
        }
    cf.axes->addPlot(std::make_unique<QuiverPlot>(x, y, u, v));
    auto img = cf.render();

    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Grid of 9 arrows should render";
}

TEST(QuiverRegression, QuiverAutoscaleMatchesPositions) {
    QuiverFigure cf(256);
    std::vector<float> x{1, 3, 5}, y{2, 4, 6}, u{1, 1, 1}, v{1, 1, 1};
    cf.axes->addPlot(std::make_unique<QuiverPlot>(x, y, u, v));
    cf.render();

    const auto& av = cf.axes->viewport();
    // Autoscale uses positions (not arrow tips), so x=[1,5], y=[2,6].
    EXPECT_NEAR(av.x.min, 0.8f, 0.1f);
    EXPECT_NEAR(av.x.max, 5.2f, 0.1f);
    EXPECT_NEAR(av.y.min, 1.8f, 0.1f);
    EXPECT_NEAR(av.y.max, 6.2f, 0.1f);
}

TEST(QuiverRegression, ArrowsHaveDirection) {
    // Two arrows: one pointing right, one pointing left.
    // They should produce pixels in different regions.
    QuiverFigure cf(256);
    cf.axes->addPlot(std::make_unique<QuiverPlot>(
        std::vector<float>{2.0f, 6.0f},
        std::vector<float>{4.0f, 4.0f},
        std::vector<float>{1.0f, -1.0f},
        std::vector<float>{0.0f, 0.0f},
        QuiverConfig{}));
    auto img = cf.render();

    // Should have black pixels (arrows).
    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 10u) << "Two arrows should render";

    // The right-pointing arrow should have pixels to the right of x=2,
    // and the left-pointing arrow should have pixels to the left of x=6.
    // Check that there are black pixels in both left and right halves.
    size_t leftBlack = 0, rightBlack = 0;
    for (uint32_t y = 0; y < img.height(); ++y) {
        for (uint32_t x = 0; x < 128; ++x)
            if (isBlack(img.get(x, y))) ++leftBlack;
        for (uint32_t x = 128; x < img.width(); ++x)
            if (isBlack(img.get(x, y))) ++rightBlack;
    }
    EXPECT_GT(leftBlack, 0u) << "Left-pointing arrow should have left-side pixels";
    EXPECT_GT(rightBlack, 0u) << "Right-pointing arrow should have right-side pixels";
}

TEST(QuiverRegression, QuiverWithCustomColor) {
    QuiverFigure cf(256);
    QuiverConfig cfg;
    cfg.color = Color::red();
    cfg.filledHeads = true;
    cf.axes->addPlot(std::make_unique<QuiverPlot>(
        std::vector<float>{3.0f},
        std::vector<float>{3.0f},
        std::vector<float>{2.0f},
        std::vector<float>{2.0f},
        cfg));
    auto img = cf.render();

    // Should have red pixels.
    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red arrow should render red pixels";
}
