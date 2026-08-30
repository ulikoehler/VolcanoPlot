// tests/test_pcolormesh.cpp — tests for PcolormeshPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/PcolormeshPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct PcmFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit PcmFigure(uint32_t size = 256)
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

/// Build a simple 2x2 grid with known values.
/// x = [0, 5, 10], y = [0, 5, 10]
/// C = [[0, 1],    (bottom row: left=0, right=1)
///      [0, 1]]    (top row:    left=0, right=1)
/// This is a horizontal gradient.
struct SimpleGrid {
    std::vector<float> x{0, 5, 10};
    std::vector<float> y{0, 5, 10};
    std::vector<float> C{0, 1, 0, 1};  // row-major: C[j*2 + i]
    uint32_t nCols = 2, nRows = 2;
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, BasicGridRenders) {
    PcmFigure cf(256);
    SimpleGrid g;
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        g.x, g.y, g.C, g.nCols, g.nRows, cfg));
    auto img = cf.render();

    // Should have many non-white pixels (4 cells fill the grid area).
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10000u) << "Pcolormesh should fill the grid area";
}

TEST(PcolormeshRegression, HorizontalGradientHasDifferentColors) {
    // Left cells (value=0) should be different from right cells (value=1).
    PcmFigure cf(256);
    SimpleGrid g;
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        g.x, g.y, g.C, g.nCols, g.nRows, cfg));
    auto img = cf.render();

    auto vp = expectedViewport(0, 10, 0, 10);
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 2.5f, 5.0f);  // center of left cells
    auto [rx, ry] = dataToPixel(vp, cf.axes->rect, 7.5f, 5.0f);  // center of right cells
    Pixel left = img.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    Pixel right = img.get(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
    EXPECT_FALSE(left.approx(right, 30))
        << "Left (value=0) and right (value=1) should have different colors";
}

TEST(PcolormeshRegression, UniformGridProducesUniformColor) {
    // All cells have the same value → uniform color.
    PcmFigure cf(256);
    std::vector<float> x{0, 5, 10};
    std::vector<float> y{0, 5, 10};
    std::vector<float> C{0.5f, 0.5f, 0.5f, 0.5f};
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, C, 2, 2, cfg));
    auto img = cf.render();

    // Check that all non-white pixels in the grid area have the same color.
    Pixel ref = img.get(128, 128);
    int mismatches = 0;
    for (uint32_t y = 64; y < 192; y += 8)
        for (uint32_t x = 64; x < 192; x += 8) {
            Pixel p = img.get(x, y);
            if (isNotWhite(p) && !p.approx(ref, 30)) ++mismatches;
        }
    EXPECT_LT(mismatches, 5) << "Uniform grid should produce uniform color";
}

TEST(PcolormeshRegression, AutoscaleMatchesEdges) {
    PcmFigure cf(256);
    SimpleGrid g;
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        g.x, g.y, g.C, g.nCols, g.nRows));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    EXPECT_NEAR(av.y.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.y.max, 10.5f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Non-uniform cells
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, NonUniformCellsRender) {
    // Irregular cell sizes: x = [0, 2, 10], y = [0, 8, 10].
    // Left column is narrow, right column is wide.
    PcmFigure cf(256);
    std::vector<float> x{0, 2, 10};
    std::vector<float> y{0, 8, 10};
    std::vector<float> C{0, 1, 0, 1};
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, C, 2, 2, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "Non-uniform grid should fill area";

    // The right column (wider) should have more pixels than the left.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 1.0f, 5.0f);
    auto [rx, ry] = dataToPixel(vp, cf.axes->rect, 6.0f, 5.0f);
    Pixel left = img.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    Pixel right = img.get(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
    EXPECT_FALSE(left.approx(right, 30))
        << "Left and right cells should have different colors";
}

// ═══════════════════════════════════════════════════════════════════════════
// NaN handling
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, NaNSkipsCell) {
    // One cell is NaN → should be skipped (white).
    // C = {0, 1, NaN, 1} → C[j*2 + i]:
    //   (i=0, j=0) = 0 (bottom-left), (i=1, j=0) = 1 (bottom-right)
    //   (i=0, j=1) = NaN (top-left),  (i=1, j=1) = 1 (top-right)
    PcmFigure cf(256);
    std::vector<float> x{0, 5, 10};
    std::vector<float> y{0, 5, 10};
    std::vector<float> C{0, 1, std::nanf(""), 1};
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.skipNaN = true;
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, C, 2, 2, cfg));
    auto img = cf.render();

    // Top-left cell (NaN) should be white.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 2.5f, 7.5f);
    Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_TRUE(p.approx(Pixel::white(), 40))
        << "NaN cell should be white (skipped)";

    // Top-right cell (value=1) should not be white.
    auto [px2, py2] = dataToPixel(vp, cf.axes->rect, 7.5f, 7.5f);
    Pixel p2 = img.get(static_cast<uint32_t>(px2), static_cast<uint32_t>(py2));
    EXPECT_FALSE(p2.approx(Pixel::white(), 40))
        << "Non-NaN cell should be colored";
}

// ═══════════════════════════════════════════════════════════════════════════
// Custom value range
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, ExplicitValueRange) {
    // With explicit value range [0, 2], value=1 maps to t=0.5 (middle of cmap).
    PcmFigure cf(256);
    std::vector<float> x{0, 5, 10};
    std::vector<float> y{0, 5, 10};
    std::vector<float> C{0, 1, 0, 1};
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {0, 2};  // value=0 → t=0, value=1 → t=0.5
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, C, 2, 2, cfg));
    auto img = cf.render();

    // Left cells (value=0, t=0) should differ from right cells (value=1, t=0.5).
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 2.5f, 5.0f);
    auto [rx, ry] = dataToPixel(vp, cf.axes->rect, 7.5f, 5.0f);
    Pixel left = img.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    Pixel right = img.get(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
    EXPECT_FALSE(left.approx(right, 30))
        << "Different values should produce different colors with explicit range";
}

// ═══════════════════════════════════════════════════════════════════════════
// Larger grid
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, LargerGridRendersGradient) {
    // 5x5 grid with a radial gradient.
    PcmFigure cf(256);
    uint32_t n = 5;
    std::vector<float> x(n + 1), y(n + 1), C(n * n);
    for (uint32_t i = 0; i <= n; ++i) {
        x[i] = float(i);
        y[i] = float(i);
    }
    for (uint32_t j = 0; j < n; ++j)
        for (uint32_t i = 0; i < n; ++i) {
            float dx = i - 2.0f, dy = j - 2.0f;
            C[j * n + i] = std::sqrt(dx * dx + dy * dy);
        }
    PcolormeshConfig cfg;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<PcolormeshPlot>(
        x, y, C, n, n, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10000u) << "5x5 grid should fill area";

    // Center cell (low value) should differ from corner cell (high value).
    auto vp = expectedViewport(0, 5, 0, 5);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 2.5f, 2.5f);
    auto [ex, ey] = dataToPixel(vp, cf.axes->rect, 0.5f, 0.5f);
    Pixel center = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    Pixel corner = img.get(static_cast<uint32_t>(ex), static_cast<uint32_t>(ey));
    EXPECT_FALSE(center.approx(corner, 30))
        << "Center and corner should have different colors";
}

// ═══════════════════════════════════════════════════════════════════════════
// Different colormaps
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolormeshRegression, DifferentColormapsProduceDifferentColors) {
    // Same data with two different colormaps should produce different colors.
    // Check at t=0 (value=0, left cells) where viridis (dark purple) and
    // plasma (dark blue) differ significantly.
    SimpleGrid g;

    PcmFigure cf1(256);
    PcolormeshConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    cf1.axes->addPlot(std::make_unique<PcolormeshPlot>(
        g.x, g.y, g.C, g.nCols, g.nRows, cfg1));
    auto img1 = cf1.render();

    PcmFigure cf2(256);
    PcolormeshConfig cfg2;
    cfg2.cmap = &colormaps::plasma();
    cf2.axes->addPlot(std::make_unique<PcolormeshPlot>(
        g.x, g.y, g.C, g.nCols, g.nRows, cfg2));
    auto img2 = cf2.render();

    auto vp = expectedViewport(0, 10, 0, 10);
    // Check left cells (value=0, t=0) where the colormaps differ most.
    auto [px, py] = dataToPixel(vp, cf1.axes->rect, 2.5f, 5.0f);
    Pixel p1 = img1.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    Pixel p2 = img2.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p1.approx(p2, 30))
        << "Viridis and plasma should produce different colors at t=0";
}
