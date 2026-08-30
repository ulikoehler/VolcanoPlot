// tests/test_tripcolor.cpp — tests for TripcolorPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/TripcolorPlot.hpp>
#include <volcano/plot/Triangulation.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct TriFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit TriFig(uint32_t size = 256)
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

/// Generate a grid of (x, y, z) points where z = x^2 + y^2 (paraboloid).
struct XYZ {
    std::vector<float> x, y, z;
};
XYZ paraboloid(int n, float scale = 1.0f) {
    XYZ d;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float x = (i / static_cast<float>(n - 1) - 0.5f) * scale;
            float y = (j / static_cast<float>(n - 1) - 0.5f) * scale;
            d.x.push_back(x);
            d.y.push_back(y);
            d.z.push_back(x * x + y * y);
        }
    return d;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(TripcolorRegression, BasicFlatRenders) {
    TriFig cf(256);
    auto d = paraboloid(5, 2.0f);
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.shading = TriShading::Flat;
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d.x), std::move(d.y), std::move(d.z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Flat tripcolor should render";
}

TEST(TripcolorRegression, BasicGouraudRenders) {
    TriFig cf(256);
    auto d = paraboloid(5, 2.0f);
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.shading = TriShading::Gouraud;
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d.x), std::move(d.y), std::move(d.z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Gouraud tripcolor should render";
}

TEST(TripcolorRegression, Autoscale) {
    TriFig cf(256);
    auto d = paraboloid(5, 2.0f);
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d.x), std::move(d.y), std::move(d.z)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -1.1f, 0.05f);
    EXPECT_NEAR(av.x.max, 1.1f, 0.05f);
    EXPECT_NEAR(av.y.min, -1.1f, 0.05f);
    EXPECT_NEAR(av.y.max, 1.1f, 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Shading modes
// ═══════════════════════════════════════════════════════════════════════════

TEST(TripcolorRegression, FlatVsGouraudDiffer) {
    // With a paraboloid, flat and gouraud should produce different images
    // because flat averages vertex values while gouraud interpolates.
    TriFig cf1(256);
    auto d1 = paraboloid(5, 2.0f);
    TripcolorConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    cfg1.shading = TriShading::Flat;
    cf1.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d1.x), std::move(d1.y), std::move(d1.z), cfg1));
    auto imgFlat = cf1.render();

    TriFig cf2(256);
    auto d2 = paraboloid(5, 2.0f);
    TripcolorConfig cfg2;
    cfg2.cmap = &colormaps::viridis();
    cfg2.shading = TriShading::Gouraud;
    cf2.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d2.x), std::move(d2.y), std::move(d2.z), cfg2));
    auto imgGouraud = cf2.render();

    // Center pixel should differ between flat and gouraud.
    Pixel pf = imgFlat.get(128, 128);
    Pixel pg = imgGouraud.get(128, 128);
    EXPECT_FALSE(pf.approx(pg, 30))
        << "Flat and gouraud should produce different colors";
}

TEST(TripcolorRegression, UniformDataUniformColor) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f, 0.5f};
    std::vector<float> y = {0, 0, 1, 0.5f};
    std::vector<float> z = {5, 5, 5, 5};  // uniform
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(z), cfg));
    auto img = cf.render();

    // All triangles should have the same color.
    Pixel center = img.get(128, 128);
    Pixel offCenter = img.get(100, 100);
    EXPECT_TRUE(center.approx(offCenter, 30))
        << "Uniform data should produce uniform color";
}

// ═══════════════════════════════════════════════════════════════════════════
// Custom colormap and value range
// ═══════════════════════════════════════════════════════════════════════════

TEST(TripcolorRegression, CustomColormap) {
    TriFig cf(256);
    auto d = paraboloid(5, 2.0f);
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d.x), std::move(d.y), std::move(d.z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Custom colormap should render";
}

TEST(TripcolorRegression, ExplicitValueRange) {
    TriFig cf(256);
    auto d = paraboloid(5, 2.0f);
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {0, 10};
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d.x), std::move(d.y), std::move(d.z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Explicit value range should render";
}

TEST(TripcolorRegression, DifferentColormapsProduceDifferentColors) {
    TriFig cf1(256);
    auto d1 = paraboloid(5, 2.0f);
    TripcolorConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    cf1.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d1.x), std::move(d1.y), std::move(d1.z), cfg1));
    auto imgViridis = cf1.render();

    TriFig cf2(256);
    auto d2 = paraboloid(5, 2.0f);
    TripcolorConfig cfg2;
    cfg2.cmap = &colormaps::plasma();
    cf2.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(d2.x), std::move(d2.y), std::move(d2.z), cfg2));
    auto imgPlasma = cf2.render();

    Pixel p1 = imgViridis.get(128, 128);
    Pixel p2 = imgPlasma.get(128, 128);
    EXPECT_FALSE(p1.approx(p2, 30))
        << "Different colormaps should produce different colors";
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-face values constructor
// ═══════════════════════════════════════════════════════════════════════════

TEST(TripcolorRegression, FacevaluesRenders) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f, 0.5f};
    std::vector<float> y = {0, 0, 1, 0.5f};
    // 2 triangles
    std::vector<Triangle> tris = {{0, 1, 2}, {0, 2, 3}};
    std::vector<float> facevalues = {0.0f, 1.0f};
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(tris), std::move(facevalues), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Per-face values should render";
}

TEST(TripcolorRegression, FacevaluesDifferentColors) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0, 1};
    std::vector<float> y = {0, 0, 1, 1};
    std::vector<Triangle> tris = {{0, 1, 2}, {1, 3, 2}};
    std::vector<float> facevalues = {0.0f, 1.0f};  // very different values
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {0, 1};
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(tris), std::move(facevalues), cfg));
    auto img = cf.render();

    // Check that two halves of the image have different colors.
    Pixel left = img.get(64, 128);
    Pixel right = img.get(192, 128);
    EXPECT_FALSE(left.approx(right, 30))
        << "Different face values should produce different colors";
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(TripcolorRegression, ThreePointsOneTriangle) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 0, 1};
    std::vector<float> z = {0, 1, 2};
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Single triangle should render";
}

TEST(TripcolorRegression, ScatteredPoints) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0, 1, 0.5f, 0.3f, 0.7f};
    std::vector<float> y = {0, 0, 1, 1, 0.5f, 0.7f, 0.3f};
    std::vector<float> z = {0, 1, 2, 3, 1.5f, 2.5f, 0.5f};
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(z), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Scattered points should render";
}

TEST(TripcolorRegression, NaNVerticesSkipTriangle) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f, 0.5f};
    std::vector<float> y = {0, 0, 1, 0.5f};
    std::vector<float> z = {0, 1, 2, NAN};  // 4th point is NaN
    TripcolorConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.shading = TriShading::Gouraud;
    cf.axes->addPlot(std::make_unique<TripcolorPlot>(
        std::move(x), std::move(y), std::move(z), cfg));
    auto img = cf.render();

    // Triangles with NaN vertex should be skipped (transparent color).
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 0u) << "Non-NaN triangles should render";
}
