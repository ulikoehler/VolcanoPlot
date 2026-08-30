// tests/test_boxplot.cpp — tests for BoxPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/BoxPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BoxFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BoxFigure(uint32_t size = 256)
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
// Basic boxplot rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(BoxPlotRegression, SingleBoxRenders) {
    // Data: 0,1,2,3,4,5,6,7,8,9,10
    // Q1=2.75, median=5, Q3=7.25, IQR=4.5
    // Whiskers: 0 to 10 (all within 1.5*IQR)
    BoxFigure cf(256);
    std::vector<float> data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    BoxPlotConfig cfg;
    cfg.boxColor = Color::fromRgba8(31, 119, 180, 200);
    cfg.boxEdgeColor = Color::fromRgba8(31, 119, 180, 255);
    cfg.whiskerColor = Color::fromRgba8(0, 200, 0, 255);  // green whiskers
    cfg.medianColor = Color::fromRgba8(255, 0, 0, 255);   // red median
    cfg.outlierColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.boxWidth = 0.5f;
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    // The box should be at x=1, spanning Q1=2.75 to Q3=7.25.
    // Viewport X: [1-0.25, 1+0.25] = [0.75, 1.25] → with padding.
    // Viewport Y: [0, 10] → with padding.
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.y.min, -0.5f, 0.1f) << "Y min should include whisker low";
    EXPECT_NEAR(av.y.max, 10.5f, 0.1f) << "Y max should include whisker high";

    // Check for green whisker pixels.
    size_t greenCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30) ++greenCount;
        }
    }
    EXPECT_GT(greenCount, 10u) << "Should have green whisker pixels";

    // Check for red median line pixels.
    size_t redCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    }
    EXPECT_GT(redCount, 5u) << "Should have red median line pixels";
}

TEST(BoxPlotRegression, MultipleBoxesRender) {
    // Two groups of data.
    BoxFigure cf(256);
    std::vector<std::vector<float>> groups = {
        {1, 2, 3, 4, 5},       // group 1: median=3
        {6, 7, 8, 9, 10},      // group 2: median=8
    };

    BoxPlotConfig cfg;
    cfg.boxColor = Color::fromRgba8(31, 119, 180, 128);
    cfg.whiskerColor = Color::black();
    cfg.medianColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.boxWidth = 0.5f;
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(groups), cfg));
    auto img = cf.render();

    // X range: [0.75, 2.25] → with padding [0.675, 2.325].
    // Y range: [1, 10] → with padding [0.55, 10.45].
    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.y.min, 0.55f, 0.2f);
    EXPECT_NEAR(av.y.max, 10.45f, 0.2f);
    EXPECT_NEAR(av.x.min, 0.675f, 0.1f);
    EXPECT_NEAR(av.x.max, 2.325f, 0.1f);

    // Should have blue box pixels (filled boxes).
    size_t blueCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.b > 100 && p.b > p.r + 20 && p.b > p.g + 20) ++blueCount;
        }
    }
    EXPECT_GT(blueCount, 100u) << "Should have blue box fill pixels";

    // Should have red median line pixels (2 medians).
    size_t redCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    }
    EXPECT_GT(redCount, 10u) << "Should have red median pixels for 2 boxes";
}

// ═══════════════════════════════════════════════════════════════════════════
// Outlier detection
// ═══════════════════════════════════════════════════════════════════════════

TEST(BoxPlotRegression, OutliersRenderAsPoints) {
    // Data with outliers: 1,2,3,4,5,6,7,8,9,10,100
    // Q1≈3.25, Q3≈8.75, IQR=5.5, upper fence = 8.75 + 1.5*5.5 = 17
    // 100 is an outlier.
    BoxFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};

    BoxPlotConfig cfg;
    cfg.outlierColor = Color::fromRgba8(255, 0, 0, 255);
    cfg.outlierSize = 6.0f;
    cfg.boxWidth = 0.5f;
    cfg.whiskerColor = Color::black();
    cfg.medianColor = Color::black();
    cfg.boxColor = Color::fromRgba8(31, 119, 180, 128);
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    // The outlier at 100 should extend the Y range significantly.
    const auto& av = cf.axes->viewport();
    EXPECT_GT(av.y.max, 90.0f) << "Y max should include outlier at 100";

    // Should have red outlier pixels (the point at y=100).
    // The outlier is at x=1, y=100. In pixel space, this is near the top.
    size_t redCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    }
    EXPECT_GT(redCount, 3u) << "Should have red outlier point pixels";
}

TEST(BoxPlotRegression, NoOutliersWhenDisabled) {
    // Same data as above, but outliers disabled.
    BoxFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};

    BoxPlotConfig cfg;
    cfg.showOutliers = false;
    cfg.boxWidth = 0.5f;
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    // Without outliers, Y range should only include whisker extents.
    // Whisker hi should be ~10 (the last point within 1.5*IQR).
    const auto& av = cf.axes->viewport();
    EXPECT_LT(av.y.max, 20.0f) << "Y max should not include outlier when disabled";
}

// ═══════════════════════════════════════════════════════════════════════════
// Whisker types
// ═══════════════════════════════════════════════════════════════════════════

TEST(BoxPlotRegression, MinMaxWhiskers) {
    // With MinMax whiskers, whiskers extend to actual min/max.
    BoxFigure cf(256);
    std::vector<float> data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};

    BoxPlotConfig cfg;
    cfg.whisker = BoxWhiskerType::MinMax;
    cfg.showOutliers = false;  // no outliers with MinMax
    cfg.boxWidth = 0.5f;
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    // With MinMax, whisker hi = 100, so Y max should be ~100.
    const auto& av = cf.axes->viewport();
    EXPECT_GT(av.y.max, 90.0f) << "MinMax whisker should extend to max=100";
    EXPECT_LT(av.y.min, 5.0f) << "MinMax whisker should extend to min=0";
}

// ═══════════════════════════════════════════════════════════════════════════
// Statistics computation
// ═══════════════════════════════════════════════════════════════════════════

TEST(BoxPlotStats, QuartilesCorrect) {
    // Data: 1,2,3,4,5,6,7,8,9,10
    // Q1 = 3.25, median = 5.5, Q3 = 7.75
    BoxFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    BoxPlotConfig cfg;
    auto* plot = cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();  // triggers prepare()

    auto* bp = static_cast<BoxPlot*>(plot);
    ASSERT_EQ(bp->stats().size(), 1u);
    const auto& s = bp->stats()[0];
    EXPECT_NEAR(s.q1, 3.25f, 0.01f) << "Q1 should be 3.25";
    EXPECT_NEAR(s.median, 5.5f, 0.01f) << "Median should be 5.5";
    EXPECT_NEAR(s.q3, 7.75f, 0.01f) << "Q3 should be 7.75";
}

TEST(BoxPlotStats, OutlierDetection) {
    // Data: 1,2,3,4,5,6,7,8,9,10,100
    // 100 should be an outlier.
    BoxFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};

    BoxPlotConfig cfg;
    auto* plot = cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    auto* bp = static_cast<BoxPlot*>(plot);
    ASSERT_EQ(bp->stats().size(), 1u);
    const auto& s = bp->stats()[0];
    ASSERT_EQ(s.outliers.size(), 1u) << "Should detect 1 outlier";
    EXPECT_NEAR(s.outliers[0], 100.0f, 0.01f) << "Outlier should be 100";
}

// ═══════════════════════════════════════════════════════════════════════════
// Box fill on/off
// ═══════════════════════════════════════════════════════════════════════════

TEST(BoxPlotRegression, BoxFillDisabled) {
    BoxFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    BoxPlotConfig cfg;
    cfg.fillBox = false;
    cfg.boxWidth = 0.5f;
    cfg.whiskerColor = Color::fromRgba8(0, 200, 0, 255);
    cfg.medianColor = Color::fromRgba8(255, 0, 0, 255);
    cf.axes->addPlot(std::make_unique<BoxPlot>(std::move(data), cfg));
    auto img = cf.render();

    // Without fill, there should be no blue box fill pixels.
    // The only colored pixels should be green whiskers and red median.
    size_t blueFillCount = 0;
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            Pixel p = img.get(x, y);
            // Blue-dominant pixels that aren't near-black (whisker) or red (median)
            if (p.b > 100 && p.b > p.r + 30 && p.b > p.g + 30 && p.a > 200)
                ++blueFillCount;
        }
    }
    // With fillBox=false, there should be very few or no blue fill pixels.
    // (The box edges use whiskerColor=green, so no blue at all.)
    EXPECT_LT(blueFillCount, 20u) << "Should not have significant blue fill when fillBox=false";
}
