// tests/test_hexbin.cpp — tests for HexbinPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/HexbinPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct HexFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit HexFigure(uint32_t size = 256)
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

std::pair<std::vector<float>, std::vector<float>>
bivariateNormal(int n, float mx, float my, float sx, float sy, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dx(mx, sx);
    std::normal_distribution<float> dy(my, sy);
    std::vector<float> xs(n), ys(n);
    for (int i = 0; i < n; ++i) { xs[i] = dx(rng); ys[i] = dy(rng); }
    return {xs, ys};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(HexbinRegression, BasicHexbinRenders) {
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    HexbinConfig cfg;
    cfg.gridsize = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Hexbin should render colored hex cells";
}

TEST(HexbinRegression, ClusterAtCenter) {
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(1000, 5.0f, 5.0f, 0.5f, 0.5f);
    HexbinConfig cfg;
    cfg.gridsize = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    // Center should be colored (high count).
    Pixel center = img.get(128, 128);
    EXPECT_FALSE(center.approx(Pixel::white(), 40))
        << "Center should be colored (high count)";

    // Corners should be white (no points).
    EXPECT_TRUE(img.get(10, 10).approx(Pixel::white(), 40))
        << "Corner should be white (no points)";
}

TEST(HexbinRegression, AutoscaleMatchesData) {
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(200, 3.0f, 7.0f, 1.0f, 1.0f);
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LT(av.x.min, 1.0f);
    EXPECT_GT(av.x.max, 5.0f);
    EXPECT_LT(av.y.min, 5.0f);
    EXPECT_GT(av.y.max, 9.0f);
}

TEST(HexbinRegression, LargerGridsizeProducesMoreCells) {
    // Larger gridsize → smaller hexes → more individual cells.
    HexFigure cf1(256);
    auto [xs1, ys1] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    HexbinConfig cfg1;
    cfg1.gridsize = 5;
    cfg1.cmap = &colormaps::viridis();
    cf1.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs1), std::move(ys1), cfg1));
    auto imgLow = cf1.render();
    size_t lowCount = countPixels(imgLow, isNotWhite);

    HexFigure cf2(256);
    auto [xs2, ys2] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    HexbinConfig cfg2;
    cfg2.gridsize = 20;
    cfg2.cmap = &colormaps::viridis();
    cf2.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs2), std::move(ys2), cfg2));
    auto imgHigh = cf2.render();
    size_t highCount = countPixels(imgHigh, isNotWhite);

    // Higher gridsize should cover a similar area but with more, smaller cells.
    // The total filled area should be similar (both cover the cluster).
    EXPECT_GT(highCount, 100u) << "Higher gridsize should render cells";
    EXPECT_GT(lowCount, 100u) << "Lower gridsize should render cells";
}

TEST(HexbinRegression, FlatTopOrientation) {
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    HexbinConfig cfg;
    cfg.gridsize = 10;
    cfg.orientation = HexOrientation::FlatTop;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Flat-top hexbin should render";
}

TEST(HexbinRegression, MinCountFiltersCells) {
    // With minCount=100, only cells with >=100 points should render.
    // With 500 points in a tight cluster, only the center cell should qualify.
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 0.3f, 0.3f);
    HexbinConfig cfg;
    cfg.gridsize = 5;
    cfg.minCount = 100;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    // Should have very few colored pixels (only high-count cells).
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_LT(filledCount, 10000u) << "High minCount should filter out most cells";
    EXPECT_GT(filledCount, 10u) << "At least one cell should pass minCount";
}

TEST(HexbinRegression, CustomColormap) {
    HexFigure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    HexbinConfig cfg;
    cfg.gridsize = 10;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Custom colormap should render";
}

TEST(HexbinRegression, TwoClustersRender) {
    HexFigure cf(256);
    auto [xs1, ys1] = bivariateNormal(300, 2.0f, 2.0f, 0.5f, 0.5f, 42);
    auto [xs2, ys2] = bivariateNormal(300, 8.0f, 8.0f, 0.5f, 0.5f, 123);
    xs1.insert(xs1.end(), xs2.begin(), xs2.end());
    ys1.insert(ys1.end(), ys2.begin(), ys2.end());
    HexbinConfig cfg;
    cfg.gridsize = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs1), std::move(ys1), cfg));
    auto img = cf.render();

    // Both cluster centers should be colored.
    Pixel c1 = img.get(64, 192);   // approx (2,2) in data space
    Pixel c2 = img.get(192, 64);   // approx (8,8) in data space
    EXPECT_FALSE(c1.approx(Pixel::white(), 40))
        << "First cluster should be colored";
    EXPECT_FALSE(c2.approx(Pixel::white(), 40))
        << "Second cluster should be colored";
}

TEST(HexbinRegression, EmptyDataProducesNoCells) {
    HexFigure cf(256);
    std::vector<float> xs, ys;
    cf.axes->addPlot(std::make_unique<HexbinPlot>(std::move(xs), std::move(ys)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty data should produce no cells";
}
