// tests/test_contour.cpp — tests for ContourPlot and ContourfPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct ContourFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit ContourFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

/// Build a simple peaked Gaussian grid for deterministic testing.
/// Center peak at (cx, cy), value = exp(-r^2 / (2*sigma^2)).
Grid2D gaussianGrid(uint32_t w, uint32_t h, float cx, float cy, float sigma) {
    Grid2D g;
    g.width = w;
    g.height = h;
    g.xRange = {0.0f, 10.0f};
    g.yRange = {0.0f, 10.0f};
    g.values.resize(w * h);
    for (uint32_t j = 0; j < h; ++j) {
        for (uint32_t i = 0; i < w; ++i) {
            float x = g.xRange.min + float(i) * g.xRange.span() / (w - 1);
            float y = g.yRange.min + float(j) * g.yRange.span() / (h - 1);
            float dx = x - cx, dy = y - cy;
            float r2 = dx * dx + dy * dy;
            g.values[j * w + i] = std::exp(-r2 / (2.0f * sigma * sigma));
        }
    }
    return g;
}

/// Build a simple linear ramp grid: value = x / xMax.
Grid2D rampGrid(uint32_t w, uint32_t h) {
    Grid2D g;
    g.width = w;
    g.height = h;
    g.xRange = {0.0f, 10.0f};
    g.yRange = {0.0f, 10.0f};
    g.values.resize(w * h);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i)
            g.values[j * w + i] = float(i) / (w - 1);
    return g;
}

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

// ═══════════════════════════════════════════════════════════════════════════
// ContourPlot — contour lines
// ═══════════════════════════════════════════════════════════════════════════

TEST(ContourRegression, ContourLinesRenderOnGaussian) {
    // A Gaussian peak should produce concentric contour lines.
    ContourFigure cf(256);
    auto grid = gaussianGrid(50, 50, 5.0f, 5.0f, 2.0f);
    ContourConfig config;
    config.numLevels = 5;
    config.lineColor = Color::black();
    config.lineWidth = 1.0f;
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(grid), config));
    auto img = cf.render();

    // Should have black contour line pixels.
    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Contour lines should produce black pixels";

    // Corners of the image (far from peak) should be white.
    EXPECT_TRUE(img.get(5, 5).approx(Pixel::white(), 40))
        << "Corner should be white (far from peak)";
}

TEST(ContourRegression, ContourLinesOnRampAreVerticalish) {
    // A linear ramp in x should produce vertical contour lines.
    ContourFigure cf(256);
    auto grid = rampGrid(50, 50);
    ContourConfig config;
    config.numLevels = 5;
    config.lineColor = Color::black();
    config.lineWidth = 2.0f;
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(grid), config));
    auto img = cf.render();

    // Should have black pixels (contour lines).
    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Ramp should produce contour lines";

    // The contour lines should be roughly vertical (spanning many y values
    // at certain x positions). Check that black pixels exist at multiple
    // y positions for a given x.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    uint32_t xMid = static_cast<uint32_t>(px);
    int yHits = 0;
    for (uint32_t y = 20; y < 236; y += 8)
        if (isBlack(img.get(xMid, y))) ++yHits;
    EXPECT_GT(yHits, 5) << "Contour line near x=5 should span vertically";
}

TEST(ContourRegression, ContourAutoscaleMatchesGrid) {
    // Autoscale should match the grid's x/y range.
    ContourFigure cf(256);
    auto grid = gaussianGrid(30, 30, 5.0f, 5.0f, 2.0f);
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(grid)));
    cf.render();  // trigger autoscale

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f) << "X min should match grid xRange";
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f) << "X max should match grid xRange";
    EXPECT_NEAR(av.y.min, -0.5f, 0.1f) << "Y min should match grid yRange";
    EXPECT_NEAR(av.y.max, 10.5f, 0.1f) << "Y max should match grid yRange";
}

TEST(ContourRegression, ExplicitLevelsRespected) {
    // With a single explicit level at 0.5, only one contour line should
    // appear on a ramp (at x=5, where value=0.5).
    ContourFigure cf(256);
    auto grid = rampGrid(50, 50);
    ContourConfig config;
    config.levels = {0.5f};
    config.lineColor = Color::black();
    config.lineWidth = 2.0f;
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(grid), config));
    auto img = cf.render();

    // The single contour line should be near x=5 (center of [0,10]).
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    uint32_t xMid = static_cast<uint32_t>(px);

    // There should be black pixels near x=5.
    bool foundLine = false;
    for (int dx = -3; dx <= 3; ++dx) {
        uint32_t x = xMid + dx;
        if (x >= img.width()) continue;
        for (uint32_t y = 0; y < img.height(); ++y)
            if (isBlack(img.get(x, y))) { foundLine = true; break; }
        if (foundLine) break;
    }
    EXPECT_TRUE(foundLine) << "Contour line at level 0.5 should appear near x=5";

    // Far from x=5, there should be no contour lines.
    auto [px2, py2] = dataToPixel(vp, cf.axes->rect, 1.0f, 5.0f);
    uint32_t xFar = static_cast<uint32_t>(px2);
    int farHits = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        if (isBlack(img.get(xFar, y))) ++farHits;
    EXPECT_LT(farHits, 5) << "No contour line expected at x=1";
}

TEST(ContourRegression, UniformGridProducesNoLines) {
    // A uniform grid (all same value) should produce no contour lines.
    ContourFigure cf(256);
    Grid2D g;
    g.width = 10;
    g.height = 10;
    g.xRange = {0, 10};
    g.yRange = {0, 10};
    g.values.assign(100, 0.5f);
    ContourConfig config;
    config.levels = {0.3f, 0.5f, 0.7f};
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(g), config));
    auto img = cf.render();

    // No black pixels (no contour lines).
    size_t blackCount = countPixels(img, isBlack);
    EXPECT_EQ(blackCount, 0u) << "Uniform grid should have no contour lines";
}

// ═══════════════════════════════════════════════════════════════════════════
// ContourfPlot — filled contour bands
// ═══════════════════════════════════════════════════════════════════════════

TEST(ContourRegression, ContourfFillsGaussian) {
    // A Gaussian peak should produce filled colored bands.
    ContourFigure cf(256);
    auto grid = gaussianGrid(50, 50, 5.0f, 5.0f, 2.0f);
    ContourConfig config;
    config.numLevels = 5;
    config.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(grid), config));
    auto img = cf.render();

    // Should have many non-white pixels (filled bands).
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "Contourf should fill a large area";

    // Center of the peak should not be white.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel center = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(center.approx(Pixel::white(), 40))
        << "Center of peak should be colored, not white";
}

TEST(ContourRegression, ContourfRampProducesGradient) {
    // A linear ramp should produce vertical color bands.
    ContourFigure cf(256);
    auto grid = rampGrid(50, 50);
    ContourConfig config;
    config.numLevels = 5;
    config.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(grid), config));
    auto img = cf.render();

    // Left and right sides should have different colors.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 1.0f, 5.0f);
    auto [rx, ry] = dataToPixel(vp, cf.axes->rect, 9.0f, 5.0f);
    Pixel left = img.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    Pixel right = img.get(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
    EXPECT_FALSE(left.approx(right, 30))
        << "Left and right of ramp should have different colors";
}

TEST(ContourRegression, ContourfAutoscaleMatchesGrid) {
    ContourFigure cf(256);
    auto grid = gaussianGrid(30, 30, 5.0f, 5.0f, 2.0f);
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(grid)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    EXPECT_NEAR(av.y.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.y.max, 10.5f, 0.1f);
}

TEST(ContourRegression, ContourfExplicitLevels) {
    // With explicit levels on a ramp, the bands should be at known positions.
    ContourFigure cf(256);
    auto grid = rampGrid(50, 50);
    ContourConfig config;
    config.levels = {0.25f, 0.5f, 0.75f};
    config.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(grid), config));
    auto img = cf.render();

    // Should have non-white pixels.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Contourf should fill the axes";

    // The center (x=5, level=0.5) should be a band boundary.
    // Just verify there are colored pixels at center.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel center = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(center.approx(Pixel::white(), 40))
        << "Center should be colored";
}

TEST(ContourRegression, ContourfUniformGridFillsEntireArea) {
    // A uniform grid should fill the grid area with one color.
    // Note: autoscale adds 5% padding, so the grid doesn't cover the
    // full canvas — the padded border remains white.
    ContourFigure cf(256);
    Grid2D g;
    g.width = 10;
    g.height = 10;
    g.xRange = {0, 10};
    g.yRange = {0, 10};
    g.values.assign(100, 0.5f);
    ContourConfig config;
    config.levels = {0.3f, 0.7f};
    config.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(g), config));
    auto img = cf.render();

    // Grid covers [0,10]×[0,10], viewport is [-0.5,10.5]×[-0.5,10.5].
    // Grid fills ~82.6% of the canvas, but rasterization edge effects
    // reduce the actual pixel count.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30000u) << "Uniform grid should fill most of the area";

    // Check that pixels within the grid area have roughly the same color.
    // Sample the center region (well within the grid).
    Pixel ref = img.get(128, 128);
    int mismatches = 0;
    for (uint32_t y = 64; y < 192; y += 8)
        for (uint32_t x = 64; x < 192; x += 8) {
            Pixel p = img.get(x, y);
            if (!p.approx(ref, 30)) ++mismatches;
        }
    EXPECT_LT(mismatches, 5) << "Uniform grid should produce uniform color in center";
}

// ═══════════════════════════════════════════════════════════════════════════
// Combined contour + contourf
// ═══════════════════════════════════════════════════════════════════════════

TEST(ContourRegression, ContourfPlusContourLines) {
    // Draw filled contours with contour lines on top.
    ContourFigure cf(256);
    auto grid1 = gaussianGrid(50, 50, 5.0f, 5.0f, 2.0f);
    auto grid2 = grid1;  // copy
    ContourConfig fconfig;
    fconfig.numLevels = 5;
    fconfig.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<ContourfPlot>(std::move(grid1), fconfig));

    ContourConfig lconfig;
    lconfig.numLevels = 5;
    lconfig.lineColor = Color::black();
    lconfig.lineWidth = 1.0f;
    cf.axes->addPlot(std::make_unique<ContourPlot>(std::move(grid2), lconfig));

    auto img = cf.render();

    // Should have both colored fills and black contour lines.
    size_t blackCount = countPixels(img, isBlack);
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "Should have filled bands";
    EXPECT_GT(blackCount, 20u) << "Should have contour lines on top";
}
