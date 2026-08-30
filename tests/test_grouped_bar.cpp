// tests/test_grouped_bar.cpp — tests for GroupedBarPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/GroupedBarPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct GBarFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit GBarFigure(uint32_t size = 256)
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

bool isOrange(const Pixel& p) {
    return p.r > 150 && p.g > 60 && p.g < 160 && p.b < 80;
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

TEST(GroupedBarRegression, BasicGroupedBarRenders) {
    GBarFigure cf(256);
    // 2 series, 3 groups
    std::vector<std::vector<float>> heights = {
        {1, 2, 3},
        {2, 3, 4}
    };
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Grouped bar should render bars";
}

TEST(GroupedBarRegression, TwoSeriesHaveDifferentColors) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {3, 3, 3},
        {3, 3, 3}
    };
    GroupedBarConfig cfg;
    cfg.colors = {Color::blue(), Color::fromRgba8(255, 127, 14, 255)};
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights), cfg));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    size_t orangeCount = countPixels(img, isOrange);
    EXPECT_GT(blueCount, 100u) << "First series should be blue";
    EXPECT_GT(orangeCount, 100u) << "Second series should be orange";
}

TEST(GroupedBarRegression, AutoscaleMatchesData) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {1, 2, 3},
        {4, 5, 6}
    };
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to 3 (3 groups)
    EXPECT_LT(av.x.min, 0.5f);
    EXPECT_GT(av.x.max, 2.5f);
    // Y: 0 to 6 (max height)
    EXPECT_LT(av.y.min, 0.5f);
    EXPECT_GT(av.y.max, 5.5f);
}

TEST(GroupedBarRegression, BarsAreSideBySide) {
    // With 2 series, bars in the same group should be adjacent.
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {5},
        {5}
    };
    GroupedBarConfig cfg;
    cfg.colors = {Color::blue(), Color::fromRgba8(255, 127, 14, 255)};
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights), cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 1}; vp.y = {0, 6}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // First series bar (left half of group 0): x ~ 0.1 to 0.4
    // Second series bar (right half): x ~ 0.5 to 0.8
    // Check that left half is blue and right half is orange.
    auto [lx, ly] = dataToPixel(vp, cf.axes->rect, 0.25f, 3.0f);
    auto [rx, ry] = dataToPixel(vp, cf.axes->rect, 0.75f, 3.0f);
    Pixel left = img2.get(static_cast<uint32_t>(lx), static_cast<uint32_t>(ly));
    Pixel right = img2.get(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
    EXPECT_TRUE(isBlue(left)) << "Left bar should be blue (first series)";
    EXPECT_TRUE(isOrange(right)) << "Right bar should be orange (second series)";
}

TEST(GroupedBarRegression, ThreeSeriesRender) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {1, 2},
        {2, 3},
        {3, 4}
    };
    GroupedBarConfig cfg;
    cfg.colors = {Color::blue(), Color::green(), Color::red()};
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Three series should render";
}

TEST(GroupedBarRegression, NegativeHeightsRender) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {-1, -2, -3},
        {-2, -3, -4}
    };
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Negative heights should render below baseline";
}

TEST(GroupedBarRegression, HorizontalOrientation) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {1, 2, 3},
        {2, 3, 4}
    };
    GroupedBarConfig cfg;
    cfg.horizontal = true;
    cfg.colors = {Color::blue(), Color::fromRgba8(255, 127, 14, 255)};
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Horizontal grouped bar should render";
}

TEST(GroupedBarRegression, CustomBarWidth) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {5},
        {5}
    };
    GroupedBarConfig cfg;
    cfg.barWidth = 0.5f;  // narrower bars
    cfg.colors = {Color::blue(), Color::fromRgba8(255, 127, 14, 255)};
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);

    // Compare with wider bars.
    GBarFigure cf2(256);
    std::vector<std::vector<float>> heights2 = {
        {5},
        {5}
    };
    GroupedBarConfig cfg2;
    cfg2.barWidth = 1.0f;  // full width
    cfg2.colors = {Color::blue(), Color::fromRgba8(255, 127, 14, 255)};
    cf2.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights2), cfg2));
    auto img2 = cf2.render();
    size_t filledCount2 = countPixels(img2, isNotWhite);

    EXPECT_LT(filledCount, filledCount2)
        << "Narrower bars should have fewer filled pixels";
}

TEST(GroupedBarRegression, DefaultColorsFromTab10) {
    GBarFigure cf(256);
    std::vector<std::vector<float>> heights = {
        {3, 3},
        {3, 3}
    };
    // No explicit colors — should use tab10 defaults.
    cf.axes->addPlot(std::make_unique<GroupedBarPlot>(std::move(heights)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Default colors should render";
}
