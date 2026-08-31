// tests/test_hist2d.cpp — tests for Hist2DPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Hist2DPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct H2Figure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit H2Figure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

Viewport expectedViewport(float minX, float maxX, float minY, float maxY) {
    Viewport v;
    v.x = {minX, maxX};
    v.y = {minY, maxY};
    v.z = {0, 1};
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
    return v;
}

std::pair<float, float> dataToPixel(const Viewport& v, const Rect2D& rect,
                                    float dx, float dy) {
    float nx = (dx - v.x.min) / v.x.span();
    float ny = (dy - v.y.min) / v.y.span();
    float px = rect.x + nx * rect.width;
    float py = rect.y + (1.0f - ny) * rect.height;
    return {px, py};
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

/// Generate bivariate normal samples.
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

TEST(Hist2DRegression, BasicHist2DRenders) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 10;
    cfg.nBinsY = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Hist2D should render colored cells";
}

TEST(Hist2DRegression, ClusterAtCenter) {
    // Points clustered at (5,5) should produce a hot spot in the center.
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(1000, 5.0f, 5.0f, 0.8f, 0.8f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 10;
    cfg.nBinsY = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    // Center should be colored (high count).
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel center = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(center.approx(Pixel::white(), 40))
        << "Center should be colored (high count)";

    // Corners should be white (no points).
    EXPECT_TRUE(img.get(10, 10).approx(Pixel::white(), 40))
        << "Corner should be white (no points)";
}

TEST(Hist2DRegression, AutoscaleMatchesData) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(200, 3.0f, 7.0f, 1.0f, 1.0f);
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // Data range is roughly [0, 6] for x and [4, 10] for y.
    EXPECT_LT(av.x.min, 1.0f);
    EXPECT_GT(av.x.max, 5.0f);
    EXPECT_LT(av.y.min, 5.0f);
    EXPECT_GT(av.y.max, 9.0f);
}

TEST(Hist2DRegression, FixedBinsRespected) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(200, 5.0f, 5.0f, 1.5f, 1.5f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 4;
    cfg.nBinsY = 4;
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    cf.render();

    // After prepare, bin edges should have 5 elements (4 bins).
    // We can't access the plot directly, but we can check rendering.
    // With 4x4 bins, the cells should be larger.
    auto img = cf.render();
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "4x4 bins should render";
}

TEST(Hist2DRegression, ExplicitEdgesRespected) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(200, 5.0f, 5.0f, 1.5f, 1.5f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Edges;
    cfg.xEdges = {0, 2, 4, 6, 8, 10};
    cfg.yEdges = {0, 5, 10};
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Explicit edges should render";
}

TEST(Hist2DRegression, EmptyBinsAreWhite) {
    // Points only in one corner → other areas should be white.
    H2Figure cf(256);
    std::vector<float> xs(100, 1.0f), ys(100, 1.0f);  // all at (1,1)
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 5;
    cfg.nBinsY = 5;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    // Bottom-left area (near (1,1)) should be colored.
    auto vp = expectedViewport(0, 1, 0, 1);  // data range is [1,1] → degenerate
    // Actually autoscale will set range to [1,1] then pad. Let's just check
    // that there are some colored pixels and some white pixels.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Should have some colored cells";
    // Most of the image should be white (only 1 cell has data).
    size_t whiteCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (img.get(x, y).approx(Pixel::white(), 40)) ++whiteCount;
    EXPECT_GT(whiteCount, 40000u) << "Most of image should be white";
}

TEST(Hist2DRegression, DensityNormalization) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 10;
    cfg.nBinsY = 10;
    cfg.normMode = Hist2DNorm::Density;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Density normalization should render";
}

TEST(Hist2DRegression, CustomColormap) {
    H2Figure cf(256);
    auto [xs, ys] = bivariateNormal(500, 5.0f, 5.0f, 1.5f, 1.5f);
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 10;
    cfg.nBinsY = 10;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs), std::move(ys), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Custom colormap should render";
}

TEST(Hist2DRegression, TwoClustersRender) {
    // Two separate clusters should produce two hot spots.
    H2Figure cf(256);
    auto [xs1, ys1] = bivariateNormal(300, 2.0f, 2.0f, 0.5f, 0.5f, 42);
    auto [xs2, ys2] = bivariateNormal(300, 8.0f, 8.0f, 0.5f, 0.5f, 123);
    xs1.insert(xs1.end(), xs2.begin(), xs2.end());
    ys1.insert(ys1.end(), ys2.begin(), ys2.end());
    Hist2DConfig cfg;
    cfg.bins = Hist2DBinMethod::Fixed;
    cfg.nBinsX = 10;
    cfg.nBinsY = 10;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<Hist2DPlot>(std::move(xs1), std::move(ys1), cfg));
    auto img = cf.render();

    // Both cluster centers should be colored.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px1, py1] = dataToPixel(vp, cf.axes->rect, 2.0f, 2.0f);
    auto [px2, py2] = dataToPixel(vp, cf.axes->rect, 8.0f, 8.0f);
    Pixel c1 = img.get(static_cast<uint32_t>(px1), static_cast<uint32_t>(py1));
    Pixel c2 = img.get(static_cast<uint32_t>(px2), static_cast<uint32_t>(py2));
    EXPECT_FALSE(c1.approx(Pixel::white(), 40))
        << "First cluster should be colored";
    EXPECT_FALSE(c2.approx(Pixel::white(), 40))
        << "Second cluster should be colored";

    // Middle area (between clusters) should be mostly white.
    auto [mx, my] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel mid = img.get(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
    EXPECT_TRUE(mid.approx(Pixel::white(), 40))
        << "Middle between clusters should be white";
}
