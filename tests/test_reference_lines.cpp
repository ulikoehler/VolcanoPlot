// tests/test_reference_lines.cpp — tests for AxhLine, AxvLine, AxhSpan,
// AxvSpan, Vlines, Hlines
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ReferenceLines.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct RefFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit RefFigure(uint32_t size = 256)
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

/// Count pixels matching a predicate.
size_t countPixels(const Image& img,
                   bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

bool isRed(const Pixel& p) { return p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30; }
bool isGreen(const Pixel& p) { return p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30; }
bool isBlue(const Pixel& p) { return p.b > 150 && p.b > p.r + 30 && p.b > p.g + 30; }
bool isGray(const Pixel& p) {
    return std::abs(int(p.r) - int(p.g)) < 20 &&
           std::abs(int(p.g) - int(p.b)) < 20 &&
           p.r > 100 && p.r < 240;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// AxhLine — horizontal line spanning entire axes
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, AxhLineSpansEntireAxes) {
    // Draw a red horizontal line at y=5, with a scatter plot to set the
    // viewport to [0,10]×[0,10].
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();  // invisible on white bg
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto* axh = cf.axes->addPlot(std::make_unique<AxhLine>(
        5.0f, Color::fromRgba8(255, 0, 0, 255), 2.0f));
    (void)axh;
    auto img = cf.render();

    // The line at y=5 should span the full width of the axes.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    uint32_t yMid = static_cast<uint32_t>(py);

    // Check left, center, and right — search a 3×3 region around each point
    // to handle pixel-level rasterization edge effects.
    auto checkRegion = [&](uint32_t cx, uint32_t cy) -> bool {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t x = cx + dx, y = cy + dy;
                if (x < img.width() && y < img.height())
                    if (img.get(x, y).approx(Pixel::red(), 60)) return true;
            }
        return false;
    };

    for (uint32_t x : {20u, 128u, 236u}) {
        EXPECT_TRUE(checkRegion(x, yMid))
            << "Red line should span at x=" << x;
    }

    // Count red pixels — should span full width.
    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(redCount, 200u) << "AxhLine should span entire width";
}

TEST(RefLineRegression, AxhLineAutoscaleOnlyY) {
    // AxhLine at y=7 should only contribute y=7 to autoscale, not x.
    RefFigure cf(256);
    cf.axes->addPlot(std::make_unique<AxhLine>(7.0f, Color::black()));
    auto img = cf.render();

    // With only an AxhLine, X is degenerate (defaults to [0,1]).
    // Y = [7,7] → zero span → expanded to [6.5, 7.5] → 5% padding → [6.45, 7.55].
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.y.min, 6.45f, 0.1f) << "Y should include 7 with padding";
    EXPECT_NEAR(av.y.max, 7.55f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════
// AxvLine — vertical line spanning entire axes
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, AxvLineSpansEntireAxes) {
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<AxvLine>(
        5.0f, Color::fromRgba8(0, 200, 0, 255), 2.0f));
    auto img = cf.render();

    // The line at x=5 should span the full height.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    uint32_t xMid = static_cast<uint32_t>(px);

    // Check top, center, and bottom — search a 3×3 region.
    auto checkRegion = [&](uint32_t cx, uint32_t cy) -> bool {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t x = cx + dx, y = cy + dy;
                if (x < img.width() && y < img.height())
                    if (img.get(x, y).approx(Pixel::green(), 60)) return true;
            }
        return false;
    };

    for (uint32_t y : {20u, 128u, 236u}) {
        EXPECT_TRUE(checkRegion(xMid, y))
            << "Green line should span at y=" << y;
    }

    size_t greenCount = countPixels(img, isGreen);
    EXPECT_GT(greenCount, 200u) << "AxvLine should span entire height";
}

TEST(RefLineRegression, AxvLineAutoscaleOnlyX) {
    RefFigure cf(256);
    cf.axes->addPlot(std::make_unique<AxvLine>(3.0f, Color::black()));
    auto img = cf.render();

    // X = [3,3] → zero span → expanded to [2.5, 3.5] → 5% padding → [2.45, 3.55].
    // Y is degenerate (defaults to [0,1]).
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, 2.45f, 0.1f) << "X should include 3 with padding";
    EXPECT_NEAR(av.x.max, 3.55f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════
// AxhSpan — horizontal filled region
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, AxhSpanFillsHorizontalBand) {
    // Fill a gray band from y=3 to y=7.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<AxhSpan>(
        3.0f, 7.0f, Color::fromRgba8(200, 200, 200, 255)));
    auto img = cf.render();

    // Center of the band should be gray.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_TRUE(isGray(p)) << "Center of span should be gray";

    // Above and below the band should be white.
    auto [ax, ay] = dataToPixel(vp, cf.axes->rect, 5.0f, 9.0f);
    Pixel ap = img.get(static_cast<uint32_t>(ax), static_cast<uint32_t>(ay));
    EXPECT_TRUE(ap.approx(Pixel::white(), 40)) << "Above span should be white";

    // Count gray pixels — should be a wide band.
    size_t grayCount = countPixels(img, isGray);
    EXPECT_GT(grayCount, 1000u) << "AxhSpan should fill a significant band";
}

// ═══════════════════════════════════════════════════════════════════════════
// AxvSpan — vertical filled region
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, AxvSpanFillsVerticalBand) {
    // Fill a blue band from x=3 to x=7.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<AxvSpan>(
        3.0f, 7.0f, Color::fromRgba8(100, 100, 255, 255)));
    auto img = cf.render();

    // Center of the band should be blue.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_TRUE(isBlue(p)) << "Center of span should be blue";

    // Left and right of the band should be white.
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 1.0f, 5.0f);
    Pixel lp = img.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    EXPECT_TRUE(lp.approx(Pixel::white(), 40)) << "Left of span should be white";

    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 1000u) << "AxvSpan should fill a significant band";
}

// ═══════════════════════════════════════════════════════════════════════════
// Vlines — collection of vertical line segments
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, VlinesRenderMultipleLines) {
    // 3 vertical red lines at x=2, 5, 8 from y=0 to y=10.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<Vlines>(
        std::vector<float>{2, 5, 8}, 0.0f, 10.0f,
        Color::fromRgba8(255, 0, 0, 255), 2.0f));
    auto img = cf.render();

    // Each line should have red pixels at its x position.
    auto vp = expectedViewport(0, 10, 0, 10);
    for (float xv : {2.0f, 5.0f, 8.0f}) {
        auto [px, py] = dataToPixel(vp, cf.axes->rect, xv, 5.0f);
        Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
        EXPECT_TRUE(p.approx(Pixel::red(), 60))
            << "Vline at x=" << xv << " should be red";
    }

    // Between the lines should be white.
    auto [bx, by] = dataToPixel(vp, cf.axes->rect, 3.5f, 5.0f);
    Pixel bp = img.get(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
    EXPECT_TRUE(bp.approx(Pixel::white(), 40)) << "Between vlines should be white";
}

// ═══════════════════════════════════════════════════════════════════════════
// Hlines — collection of horizontal line segments
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, HlinesRenderMultipleLines) {
    // 3 horizontal green lines at y=2, 5, 8 from x=0 to x=10.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<Hlines>(
        std::vector<float>{2, 5, 8}, 0.0f, 10.0f,
        Color::fromRgba8(0, 200, 0, 255), 2.0f));
    auto img = cf.render();

    // Each line should have green pixels at its y position.
    auto vp = expectedViewport(0, 10, 0, 10);
    for (float yv : {2.0f, 5.0f, 8.0f}) {
        auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, yv);
        Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
        EXPECT_TRUE(p.approx(Pixel::green(), 60))
            << "Hline at y=" << yv << " should be green";
    }

    // Between the lines should be white.
    auto [bx, by] = dataToPixel(vp, cf.axes->rect, 5.0f, 3.5f);
    Pixel bp = img.get(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
    EXPECT_TRUE(bp.approx(Pixel::white(), 40)) << "Between hlines should be white";
}

// ═══════════════════════════════════════════════════════════════════════════
// Combined: AxvLine + AxhSpan overlay
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, CombinedAxvLineAndAxhSpan) {
    // Draw a gray span from y=3 to y=7, and a red vertical line at x=5.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<AxhSpan>(
        3.0f, 7.0f, Color::fromRgba8(200, 200, 200, 255)));
    cf.axes->addPlot(std::make_unique<AxvLine>(
        5.0f, Color::fromRgba8(255, 0, 0, 255), 2.0f));
    auto img = cf.render();

    // Should have both gray (span) and red (line) pixels.
    size_t grayCount = countPixels(img, isGray);
    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(grayCount, 500u) << "Should have gray span pixels";
    EXPECT_GT(redCount, 100u) << "Should have red line pixels";
}

// ═══════════════════════════════════════════════════════════════════════════
// AxhSpan with alpha blending
// ═══════════════════════════════════════════════════════════════════════════

TEST(RefLineRegression, AxhSpanAlphaBlended) {
    // 50% alpha gray span over white → blended to light gray.
    RefFigure cf(256);
    Series2D s;
    s.color = Color::white();
    s.points = {{0, 0}, {10, 10}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->addPlot(std::make_unique<AxhSpan>(
        3.0f, 7.0f, Color::fromRgba8(100, 100, 100, 128)));
    auto img = cf.render();

    // Center should be blended: ~(178, 178, 178) — light gray.
    auto vp = expectedViewport(0, 10, 0, 10);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_GT(p.r, 150) << "Blended gray should be light";
    EXPECT_LT(p.r, 220) << "Blended gray should not be white";
}
