// tests/test_step_stairs.cpp — tests for StepPlot and StairsPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/StepPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct StepFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit StepFigure(uint32_t size = 256)
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

bool isBlue(const Pixel& p) { return p.b > 150 && p.b > p.r + 30 && p.b > p.g + 30; }

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// StepPlot
// ═══════════════════════════════════════════════════════════════════════════

TEST(StepRegression, StepPreRenders) {
    // y = [1, 3, 2] at x = [0, 5, 10], where='pre'.
    // Pre: at x=5, y stays at 1 then jumps to 3.
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<float>{1, 3, 2},
        StepWhere::Pre, Color::blue(), 2.0f));
    auto img = cf.render();

    // Should have blue pixels (the step line).
    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 100u) << "Step plot should render blue pixels";
}

TEST(StepRegression, StepPostRenders) {
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<float>{1, 3, 2},
        StepWhere::Post, Color::blue(), 2.0f));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 100u) << "Step post should render blue pixels";
}

TEST(StepRegression, StepMidRenders) {
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<float>{1, 3, 2},
        StepWhere::Mid, Color::blue(), 2.0f));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 100u) << "Step mid should render blue pixels";
}

TEST(StepRegression, StepAutoscaleMatchesData) {
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<float>{1, 3, 2},
        StepWhere::Pre, Color::blue(), 2.0f));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    EXPECT_NEAR(av.y.min, 0.95f, 0.1f);
    EXPECT_NEAR(av.y.max, 3.05f, 0.1f);
}

TEST(StepRegression, StepPreHasHorizontalSegment) {
    // With where='pre', at x=5 the y value should be 1 (from previous point)
    // before jumping to 3. Check that there's a blue pixel at (x=5, y=1).
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StepPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<float>{1, 3, 2},
        StepWhere::Pre, Color::blue(), 2.0f));
    auto img = cf.render();

    auto vp = expectedViewport(0, 10, 1, 3);
    // At x=5 (just before the step), y should be 1.
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 4.5f, 1.0f);
    bool found = false;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            uint32_t x = px + dx, y = py + dy;
            if (x < img.width() && y < img.height())
                if (isBlue(img.get(x, y))) found = true;
        }
    EXPECT_TRUE(found) << "Pre step should have y=1 at x=4.5";
}

// ═══════════════════════════════════════════════════════════════════════════
// StairsPlot
// ═══════════════════════════════════════════════════════════════════════════

TEST(StepRegression, StairsRenders) {
    // values = [2, 4, 3], edges = [0, 3, 7, 10].
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{2, 4, 3},
        std::vector<float>{0, 3, 7, 10},
        Color::blue(), 2.0f, false));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 100u) << "Stairs plot should render blue pixels";
}

TEST(StepRegression, StairsFillRenders) {
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{2, 4, 3},
        std::vector<float>{0, 3, 7, 10},
        Color::blue(), 2.0f, true,
        Color::fromRgba8(31, 119, 180, 128)));
    auto img = cf.render();

    // Should have non-white pixels (fill + outline).
    // The fill is semi-transparent blue, so check for non-white instead.
    size_t filledCount = countPixels(img, [](const Pixel& p) {
        return !(p.r > 230 && p.g > 230 && p.b > 230);
    });
    EXPECT_GT(filledCount, 500u) << "Filled stairs should have many non-white pixels";
}

TEST(StepRegression, StairsAutoscaleMatchesData) {
    StepFigure cf(256);
    cf.axes->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{2, 4, 3},
        std::vector<float>{0, 3, 7, 10},
        Color::blue(), 2.0f, true));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    // Y includes 0 (baseline) and max value 4.
    EXPECT_NEAR(av.y.min, -0.2f, 0.1f);
    EXPECT_NEAR(av.y.max, 4.2f, 0.1f);
}
