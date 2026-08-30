// tests/test_broken_barh.cpp — tests for BrokenBarHPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/BrokenBarHPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BBHFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BBHFigure(uint32_t size = 256)
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

bool isBlue(const Pixel& p) {
    return p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20;
}

bool isRed(const Pixel& p) {
    return p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30;
}

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(BrokenBarHRegression, BasicBrokenBarHRenders) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {0, 5, 0, 1},    // bar at y=[0,1], x=[0,5]
        {2, 3, 2, 1},    // bar at y=[2,3], x=[2,5]
        {4, 4, 4, 1}     // bar at y=[4,5], x=[4,8]
    };
    BrokenBarHConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Broken barh should render bars";
}

TEST(BrokenBarHRegression, AutoscaleMatchesSegments) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {1, 3, 0, 1},   // x=[1,4], y=[0,1]
        {2, 5, 2, 1}    // x=[2,7], y=[2,3]
    };
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X range: [1, 7] with padding
    EXPECT_LT(av.x.min, 1.5f);
    EXPECT_GT(av.x.max, 6.5f);
    // Y range: [0, 3] with padding
    EXPECT_LT(av.y.min, 0.5f);
    EXPECT_GT(av.y.max, 2.5f);
}

TEST(BrokenBarHRegression, BarsAtDifferentYPositions) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {0, 10, 0, 1},   // bottom bar
        {0, 10, 5, 1}    // top bar
    };
    BrokenBarHConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 10}; vp.y = {0, 6}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Bottom bar at y=[0,1] → lower part of image.
    auto [bx, by] = dataToPixel(vp, cf.axes->rect, 5.0f, 0.5f);
    Pixel bottom = img2.get(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
    EXPECT_TRUE(isBlue(bottom)) << "Bottom bar should be blue";

    // Top bar at y=[5,6] → upper part of image.
    auto [tx, ty] = dataToPixel(vp, cf.axes->rect, 5.0f, 5.5f);
    Pixel top = img2.get(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
    EXPECT_TRUE(isBlue(top)) << "Top bar should be blue";

    // Middle area (y=2.5) should be white (no bar).
    auto [mx, my] = dataToPixel(vp, cf.axes->rect, 5.0f, 2.5f);
    Pixel mid = img2.get(static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
    EXPECT_FALSE(isBlue(mid)) << "Middle area should be white (no bar)";
}

TEST(BrokenBarHRegression, PerSegmentColors) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {0, 5, 0, 1},
        {0, 5, 2, 1}
    };
    BrokenBarHConfig cfg;
    cfg.colors = {Color::blue(), Color::red()};
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(blueCount, 100u) << "First segment should be blue";
    EXPECT_GT(redCount, 100u) << "Second segment should be red";
}

TEST(BrokenBarHRegression, GanttChartPattern) {
    // Simulate a Gantt chart with 3 tasks at different times.
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {0, 3, 0, 0.8f},   // Task 1: x=[0,3]
        {2, 4, 1, 0.8f},   // Task 2: x=[2,6]
        {5, 3, 2, 0.8f}    // Task 3: x=[5,8]
    };
    BrokenBarHConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Gantt chart should render";
}

TEST(BrokenBarHRegression, EmptySegmentsRendersNothing) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs;
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty segments should render nothing";
}

TEST(BrokenBarHRegression, OverlappingBarsRender) {
    // Bars at the same y level with overlapping x ranges.
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {0, 5, 0, 1},
        {3, 5, 0, 1}   // overlaps with first bar
    };
    BrokenBarHConfig cfg;
    cfg.colors = {Color::blue(), Color::red()};
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Overlapping bars should render";
}

TEST(BrokenBarHRegression, NegativeXValuesRender) {
    BBHFigure cf(256);
    std::vector<BarHSegment> segs = {
        {-5, 3, 0, 1},   // x=[-5, -2]
        {0, 4, 2, 1}     // x=[0, 4]
    };
    BrokenBarHConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<BrokenBarHPlot>(std::move(segs), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Negative x values should render";
}
