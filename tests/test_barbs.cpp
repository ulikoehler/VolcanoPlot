// tests/test_barbs.cpp — tests for BarbsPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/BarbsPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BarbsFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BarbsFig(uint32_t size = 256)
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

bool isBlack(const Pixel& p) {
    return p.r < 80 && p.g < 80 && p.b < 80;
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
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(BarbsRegression, BasicBarbsRender) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {10};  // 10 kt eastward
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 5u) << "Basic barb should render";
}

TEST(BarbsRegression, MultipleBarbsRender) {
    BarbsFig cf(256);
    std::vector<float> x = {0.2f, 0.5f, 0.8f};
    std::vector<float> y = {0.5f, 0.5f, 0.5f};
    std::vector<float> u = {10, 20, 30};
    std::vector<float> v = {0, 5, -5};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 15u) << "Multiple barbs should render";
}

TEST(BarbsRegression, ZeroWindNoBarb) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {0};
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_EQ(darkCount, 0u) << "Zero wind should render no barb";
}

TEST(BarbsRegression, HighSpeedFlagRendered) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {50};  // 50 kt → flag
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 10u) << "50 kt barb with flag should render";
}

TEST(BarbsRegression, CustomColor) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {15};
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red barb should render red pixels";
}

TEST(BarbsRegression, Autoscale) {
    BarbsFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 1, 0.5f};
    std::vector<float> u = {10, 20, 15};
    std::vector<float> v = {5, -5, 0};
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.05f, 0.03f);
    EXPECT_NEAR(av.x.max, 1.05f, 0.03f);
    EXPECT_NEAR(av.y.min, -0.05f, 0.03f);
    EXPECT_NEAR(av.y.max, 1.05f, 0.03f);
}

TEST(BarbsRegression, DiagonalWind) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {10};  // NE wind
    std::vector<float> v = {10};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 5u) << "Diagonal wind barb should render";
}

TEST(BarbsRegression, FlipDirection) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {10};
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cfg.flip = true;
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 5u) << "Flipped barb should render";
}

TEST(BarbsRegression, EmptyDataRendersNothing) {
    BarbsFig cf(256);
    std::vector<float> x, y, u, v;
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_EQ(darkCount, 0u) << "Empty data should render nothing";
}

TEST(BarbsRegression, GridOfBarbs) {
    BarbsFig cf(256);
    // 3x3 grid of barbs
    std::vector<float> x, y, u, v;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            x.push_back((i + 1) * 0.25f);
            y.push_back((j + 1) * 0.25f);
            u.push_back(10 + i * 5);
            v.push_back(j * 5 - 5);
        }
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 30u) << "Grid of barbs should render many segments";
}

TEST(BarbsRegression, VeryHighSpeedMultipleFlags) {
    BarbsFig cf(256);
    std::vector<float> x = {0.5f};
    std::vector<float> y = {0.5f};
    std::vector<float> u = {120};  // 120 kt → 2 flags + 2 full barbs
    std::vector<float> v = {0};
    BarbsConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarbsPlot>(
        std::move(x), std::move(y), std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 20u) << "High speed with multiple flags should render";
}
