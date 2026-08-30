// tests/test_errorbar.cpp — tests for ErrorbarPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ErrorbarPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct ErrorbarFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit ErrorbarFigure(uint32_t size = 256)
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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic errorbar rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, VerticalErrorBarsRender) {
    // 3 points with vertical error bars.
    // Points: (1, 5), (5, 5), (9, 5) with yerr = 2 each.
    ErrorbarFigure cf(256);
    std::vector<float> x = {1, 5, 9};
    std::vector<float> y = {5, 5, 5};
    ErrorbarConfig cfg;
    cfg.yerr = {2, 2, 2};
    cfg.color = Color::fromRgba8(31, 119, 180, 255);  // blue line
    cfg.markerColor = Color::fromRgba8(255, 0, 0, 255);  // red markers
    cfg.errorbarColor = Color::fromRgba8(0, 200, 0, 255);  // green error bars
    cfg.drawLine = true;
    cfg.drawMarker = true;
    cfg.drawCaps = true;
    cfg.capSize = 5;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // The viewport should include error bar extents: x=[1,9], y=[3,7].
    auto vp = expectedViewport(1.0f, 9.0f, 3.0f, 7.0f);

    // Check that green error bar pixels exist (vertical lines at x=1,5,9).
    size_t greenCount = 0;
    for (uint32_t y2 = 0; y2 < 256; ++y2) {
        for (uint32_t x2 = 0; x2 < 256; ++x2) {
            Pixel p = img.get(x2, y2);
            if (p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30) ++greenCount;
        }
    }
    EXPECT_GT(greenCount, 20u) << "Should have green error bar pixels";

    // Check that red marker pixels exist at the data points.
    auto [mx, my] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.0f);
    Pixel mp = img.get(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
    EXPECT_TRUE(mp.approx(Pixel::red(), 60)) << "Center marker should be red";
}

TEST(ErrorbarRegression, HorizontalErrorBarsRender) {
    // 3 points with horizontal error bars.
    ErrorbarFigure cf(256);
    std::vector<float> x = {5, 5, 5};
    std::vector<float> y = {1, 5, 9};
    ErrorbarConfig cfg;
    cfg.xerr = {2, 2, 2};
    cfg.color = Color::fromRgba8(31, 119, 180, 255);
    cfg.markerColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.errorbarColor = Color::fromRgba8(0, 200, 0, 255);
    cfg.drawLine = false;  // vertical line would be weird
    cfg.drawMarker = true;
    cfg.drawCaps = true;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Should have green horizontal error bar pixels.
    size_t greenCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30) ++greenCount;
        }
    }
    EXPECT_GT(greenCount, 20u) << "Should have green horizontal error bar pixels";
}

TEST(ErrorbarRegression, BothXandYErrorBars) {
    // Points with both x and y error bars.
    ErrorbarFigure cf(256);
    std::vector<float> x = {3, 7};
    std::vector<float> y = {3, 7};
    ErrorbarConfig cfg;
    cfg.xerr = {1, 1};
    cfg.yerr = {1, 1};
    cfg.color = Color::fromRgba8(31, 119, 180, 255);
    cfg.markerColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.errorbarColor = Color::fromRgba8(0, 200, 0, 255);
    cfg.drawLine = true;
    cfg.drawMarker = true;
    cfg.drawCaps = true;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Viewport should include error extents: x=[2,8], y=[2,8].
    auto vp = expectedViewport(2.0f, 8.0f, 2.0f, 8.0f);
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, vp.x.min, 0.1f);
    EXPECT_NEAR(av.x.max, vp.x.max, 0.1f);
    EXPECT_NEAR(av.y.min, vp.y.min, 0.1f);
    EXPECT_NEAR(av.y.max, vp.y.max, 0.1f);

    // Should have green pixels (error bars) and red pixels (markers).
    size_t greenCount = 0, redCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30) ++greenCount;
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    }
    EXPECT_GT(greenCount, 20u) << "Should have green error bar pixels";
    EXPECT_GT(redCount, 5u) << "Should have red marker pixels";
}

// ═══════════════════════════════════════════════════════════════════════════
// Autoscale includes error bar extents
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, AutoscaleIncludesErrorExtents) {
    // Points at y=5 with yerr=3. Viewport Y should include [2, 8].
    ErrorbarFigure cf(256);
    std::vector<float> x = {1, 5, 9};
    std::vector<float> y = {5, 5, 5};
    ErrorbarConfig cfg;
    cfg.yerr = {3, 3, 3};
    cfg.drawLine = false;
    cfg.drawMarker = true;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Without errors, Y would be [5, 5] → degenerate. With yerr=3,
    // Y should be [2, 8] → after 5% padding: [1.7, 8.3].
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.y.min, 1.7f, 0.3f) << "Y min should include error bar lower extent";
    EXPECT_NEAR(av.y.max, 8.3f, 0.3f) << "Y max should include error bar upper extent";
}

// ═══════════════════════════════════════════════════════════════════════════
// Asymmetric error bars
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, AsymmetricErrorBars) {
    // Point at (5, 5) with asymmetric yerr: lower=1, upper=3.
    // Y range should be [4, 8].
    ErrorbarFigure cf(256);
    std::vector<float> x = {5};
    std::vector<float> y = {5};
    ErrorbarConfig cfg;
    cfg.yerrLower = {1};
    cfg.yerrUpper = {3};
    cfg.drawLine = false;
    cfg.drawMarker = true;
    cfg.errorbarColor = Color::fromRgba8(0, 200, 0, 255);
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Y range should be [4, 8] → after 5% padding: [3.8, 8.2].
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.y.min, 3.8f, 0.3f) << "Y min should be 4 - padding";
    EXPECT_NEAR(av.y.max, 8.2f, 0.3f) << "Y max should be 8 + padding";
}

// ═══════════════════════════════════════════════════════════════════════════
// No error bars (just line + markers)
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, NoErrorBarsJustLineAndMarkers) {
    // No error bars — should render as a line + marker plot.
    ErrorbarFigure cf(256);
    std::vector<float> x = {1, 5, 9};
    std::vector<float> y = {1, 9, 1};
    ErrorbarConfig cfg;
    cfg.color = Color::fromRgba8(255, 0, 0, 255);
    cfg.markerColor = Color::fromRgba8(0, 0, 255, 255);
    cfg.drawLine = true;
    cfg.drawMarker = true;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Should have red line pixels and blue marker pixels.
    size_t redCount = 0, blueCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
            if (p.b > 150 && p.b > p.r + 30 && p.b > p.g + 30) ++blueCount;
        }
    }
    EXPECT_GT(redCount, 20u) << "Should have red line pixels";
    EXPECT_GT(blueCount, 5u) << "Should have blue marker pixels";
}

// ═══════════════════════════════════════════════════════════════════════════
// Caps on/off
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, CapsDisabled) {
    // With caps disabled, error bars should still render but without
    // the horizontal cap lines.
    ErrorbarFigure cf(256);
    std::vector<float> x = {3, 7};
    std::vector<float> y = {5, 5};
    ErrorbarConfig cfg;
    cfg.yerr = {3, 3};
    cfg.drawCaps = false;
    cfg.drawLine = false;
    cfg.drawMarker = false;
    cfg.errorbarColor = Color::fromRgba8(0, 200, 0, 255);
    cfg.errorbarWidth = 2.0f;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Should still have green pixels (the vertical error bar lines).
    size_t greenCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30) ++greenCount;
        }
    }
    EXPECT_GT(greenCount, 5u) << "Error bar line should render without caps";
}

// ═══════════════════════════════════════════════════════════════════════════
// Markers only (no line, no error bars)
// ═══════════════════════════════════════════════════════════════════════════

TEST(ErrorbarRegression, MarkersOnlyNoLineNoErrors) {
    ErrorbarFigure cf(256);
    std::vector<float> x = {2, 5, 8};
    std::vector<float> y = {2, 8, 2};
    ErrorbarConfig cfg;
    cfg.markerColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.drawLine = false;
    cfg.drawMarker = true;
    cf.axes->addPlot(std::make_unique<ErrorbarPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Should have red marker pixels at the 3 data points.
    auto vp = expectedViewport(2.0f, 8.0f, 2.0f, 8.0f);
    auto [mx, my] = dataToPixel(vp, cf.axes->rect, 5.0f, 8.0f);
    Pixel mp = img.get(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
    EXPECT_TRUE(mp.approx(Pixel::red(), 60)) << "Middle marker should be red";

    // Count red pixels — should be at least 3 markers worth.
    size_t redCount = img.countColor(Pixel::red(), 60);
    EXPECT_GT(redCount, 10u) << "Should have at least 3 markers";
}
