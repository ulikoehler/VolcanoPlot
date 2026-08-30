// tests/test_fill.cpp — tests for FillPlot and FillBetweenPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/FillPlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

/// Minimal self-contained crafted-figure helper for fill tests.
struct FillFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit FillFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

/// Expected viewport after Axes::finalizeAutoscale: 5% padding around bbox.
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

/// Convert data coords to pixel coords (Y-up convention).
std::pair<float, float> dataToPixel(const Viewport& v, const Rect2D& rect,
                                    float dx, float dy) {
    float nx = (dx - v.x.min) / v.x.span();
    float ny = (dy - v.y.min) / v.y.span();
    float px = rect.x + nx * rect.width;
    float py = rect.y + (1.0f - ny) * rect.height;
    return {px, py};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// FillPlot — filled polygon
// ═══════════════════════════════════════════════════════════════════════════

TEST(FillRegression, FillPolygonRendersColoredArea) {
    // A triangle: (0,0), (10,0), (5,10). Fill with red.
    // The rendered area should contain red pixels in the center.
    FillFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(255, 0, 0, 255);
    s.points.push_back({0.0f, 0.0f});
    s.points.push_back({10.0f, 0.0f});
    s.points.push_back({5.0f, 10.0f});
    cf.axes->addPlot(std::make_unique<FillPlot>(std::move(s)));
    auto img = cf.render();

    // The centroid of the triangle should be red.
    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 10.0f);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 3.33f);  // centroid
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_TRUE(p.approx(Pixel::red(), 60)) << "Center of triangle should be red";

    // Count red pixels — should be a significant area.
    size_t redCount = img.countColor(Pixel::red(), 60);
    EXPECT_GT(redCount, 100u) << "Triangle should fill a significant area";
}

TEST(FillRegression, FillPolygonRespectsAlpha) {
    // A filled square with 50% alpha over a white background should
    // produce pixels that are a blend of red and white.
    FillFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(255, 0, 0, 128);  // 50% alpha
    s.points.push_back({0.0f, 0.0f});
    s.points.push_back({10.0f, 0.0f});
    s.points.push_back({10.0f, 10.0f});
    s.points.push_back({0.0f, 10.0f});
    cf.axes->addPlot(std::make_unique<FillPlot>(std::move(s)));
    auto img = cf.render();

    // Center should be a blend: 0.5*255 + 0.5*255 = 255 for white bg
    // R = 0.5*255 + 0.5*255 = 255... no, alpha blend: src*alpha + dst*(1-alpha)
    // R = 255*0.5 + 255*0.5 = 255 (wrong, bg is white=255)
    // Actually: R = 255*128/255 + 255*(1-128/255) = 128 + 127 = 255
    // Hmm, with white bg, red with 50% alpha = (255, 127, 127, 255)
    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 10.0f);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    // Red channel should be high (near 255), green/blue should be ~127
    EXPECT_GT(p.r, 200) << "Red channel should be high";
    EXPECT_LT(p.g, 180) << "Green channel should be blended down";
    EXPECT_LT(p.b, 180) << "Blue channel should be blended down";
}

TEST(FillRegression, FillPolygonAutoscalesViewport) {
    // Fill a polygon with known bbox; verify the viewport matches.
    FillFigure cf(256);
    Series2D s;
    s.color = Color::fromRgba8(0, 200, 0, 255);
    s.points.push_back({2.0f, 3.0f});
    s.points.push_back({8.0f, 3.0f});
    s.points.push_back({8.0f, 7.0f});
    s.points.push_back({2.0f, 7.0f});
    cf.axes->addPlot(std::make_unique<FillPlot>(std::move(s)));
    auto img = cf.render();

    auto vp = expectedViewport(2.0f, 8.0f, 3.0f, 7.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 0.01f);
    EXPECT_NEAR(av.x.max, vp.x.max, 0.01f);
    EXPECT_NEAR(av.y.min, vp.y.min, 0.01f);
    EXPECT_NEAR(av.y.max, vp.y.max, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════
// FillBetweenPlot — fill between two curves
// ═══════════════════════════════════════════════════════════════════════════

TEST(FillRegression, FillBetweenCurveAndBaseline) {
    // Fill between y=x^2 and y=0 (baseline).
    // The area under the parabola should be filled.
    FillFigure cf(256);
    std::vector<float> x, y;
    for (int i = 0; i <= 10; ++i) {
        float xv = float(i);
        x.push_back(xv);
        y.push_back(xv * xv * 0.1f);  // 0 to 10
    }
    cf.axes->addPlot(std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y),
        Color::fromRgba8(31, 119, 180, 200)));
    auto img = cf.render();

    // The center of the fill area should be blue-ish.
    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 10.0f);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 2.5f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_GT(p.b, 100) << "Fill area should have blue component";
    EXPECT_LT(p.r, 100) << "Fill area should not be red-dominant";

    // Count blue-ish pixels — should be a significant area.
    Pixel blueish{31, 119, 180, 255};
    size_t blueCount = img.countColor(blueish, 80);
    EXPECT_GT(blueCount, 200u) << "Fill between should cover significant area";
}

TEST(FillRegression, FillBetweenTwoCurves) {
    // Fill between y=10 (upper) and y=0 (lower) — a horizontal band.
    FillFigure cf(256);
    std::vector<float> x, y1, y2;
    for (int i = 0; i <= 10; ++i) {
        x.push_back(float(i));
        y1.push_back(10.0f);
        y2.push_back(0.0f);
    }
    cf.axes->addPlot(std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(0, 255, 0, 255)));
    auto img = cf.render();

    // The entire axes area should be green (filled band spanning full height).
    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 10.0f);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel p = img.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    EXPECT_TRUE(p.approx(Pixel::green(), 60)) << "Center should be green";

    // Most of the canvas should be green.
    size_t greenCount = img.countColor(Pixel::green(), 60);
    EXPECT_GT(greenCount, 5000u) << "Full-height band should cover most of canvas";
}

TEST(FillRegression, FillBetweenAutoscalesViewport) {
    // Fill between y1 and y2 with known bbox.
    FillFigure cf(256);
    std::vector<float> x, y1, y2;
    for (int i = 0; i <= 5; ++i) {
        x.push_back(float(i) * 2.0f);  // 0,2,4,6,8,10
        y1.push_back(float(i) * 3.0f); // 0,3,6,9,12,15
        y2.push_back(float(i));        // 0,1,2,3,4,5
    }
    cf.axes->addPlot(std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(100, 100, 255, 255)));
    auto img = cf.render();

    auto vp = expectedViewport(0.0f, 10.0f, 0.0f, 15.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 0.01f);
    EXPECT_NEAR(av.x.max, vp.x.max, 0.01f);
    EXPECT_NEAR(av.y.min, vp.y.min, 0.01f);
    EXPECT_NEAR(av.y.max, vp.y.max, 0.01f);
}

TEST(FillRegression, FillBetweenWithLineOverlay) {
    // Fill between a curve and baseline, with a line on top.
    // The line should be visible above the fill.
    FillFigure cf(256);
    std::vector<float> x, y;
    for (int i = 0; i <= 10; ++i) {
        x.push_back(float(i));
        y.push_back(float(i));
    }
    cf.axes->addPlot(std::make_unique<FillBetweenPlot>(
        std::vector<float>{x}, std::vector<float>{y},
        Color::fromRgba8(31, 119, 180, 128)));

    Series2D line;
    line.color = Color::fromRgba8(255, 0, 0);
    line.lineWidth = 2.0f;
    line.points.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        line.points.push_back({x[i], y[i]});
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(line)));

    auto img = cf.render();

    // There should be both blue (fill) and red (line) pixels.
    // The fill uses 50% alpha, so blended with white it becomes ~(143,187,217).
    // Use a wider tolerance to catch the blended fill color.
    size_t blueCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            // Blue-dominant pixel (fill blended with white).
            if (p.b > 150 && p.b > p.r && p.b > p.g) ++blueCount;
        }
    }
    size_t redCount = img.countColor(Pixel::red(), 60);
    EXPECT_GT(blueCount, 100u) << "Fill area should have blue-ish pixels";
    EXPECT_GT(redCount, 10u) << "Line should have red pixels on top of fill";
}
