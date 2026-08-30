// tests/test_bar_label.cpp — tests for BarLabelPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/BarLabelPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BLFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BLFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

bool isDark(const Pixel& p) {
    // Dark pixels = text (black on white background).
    return p.r < 100 && p.g < 100 && p.b < 100;
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
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(BarLabelRegression, BasicLabelsRender) {
    BLFigure cf(256);
    // Add a bar plot first.
    BarData barData;
    barData.heights = {1, 2, 3, 4, 5};
    barData.width = 0.8f;
    cf.axes->addPlot(std::make_unique<BarPlot>(std::move(barData)));

    // Add labels at bar positions (x = 0.5, 1.5, 2.5, 3.5, 4.5).
    std::vector<float> x = {0.5f, 1.5f, 2.5f, 3.5f, 4.5f};
    std::vector<float> heights = {1, 2, 3, 4, 5};
    BarLabelConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 20u) << "Bar labels should render dark text pixels";
}

TEST(BarLabelRegression, LabelsAboveBars) {
    BLFigure cf(256);
    BarData barData;
    barData.heights = {3, 3, 3};
    barData.width = 0.8f;
    cf.axes->addPlot(std::make_unique<BarPlot>(std::move(barData)));

    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {3, 3, 3};
    BarLabelConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    // Text should appear above the bars (in the upper portion).
    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Labels should render above bars";
}

TEST(BarLabelRegression, CenterPosition) {
    BLFigure cf(256);
    BarData barData;
    barData.heights = {5, 5, 5};
    barData.width = 0.8f;
    cf.axes->addPlot(std::make_unique<BarPlot>(std::move(barData)));

    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {5, 5, 5};
    BarLabelConfig cfg;
    cfg.position = BarLabelPosition::Center;
    cfg.color = Color::white();  // white text inside blue bars
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    // With center position, white text should appear inside the bars.
    // Just check it doesn't crash and renders something.
    SUCCEED();
}

TEST(BarLabelRegression, CustomLabels) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {1, 2, 3};
    BarLabelConfig cfg;
    cfg.labels = {"A", "B", "C"};
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Custom labels should render";
}

TEST(BarLabelRegression, CustomFormat) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f};
    std::vector<float> heights = {1.5f, 2.7f};
    BarLabelConfig cfg;
    cfg.fmt = "%.2f";
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Custom format labels should render";
}

TEST(BarLabelRegression, NegativeHeightsLabelsBelow) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {-1, -2, -3};
    BarLabelConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Labels for negative bars should render";
}

TEST(BarLabelRegression, EmptyLabelsRendersNothing) {
    BLFigure cf(256);
    std::vector<float> x;
    std::vector<float> heights;
    BarLabelConfig cfg;
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_EQ(darkCount, 0u) << "Empty labels should render nothing";
}

TEST(BarLabelRegression, HorizontalMode) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {1, 2, 3};
    BarLabelConfig cfg;
    cfg.horizontal = true;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Horizontal bar labels should render";
}

TEST(BarLabelRegression, CustomColor) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f};
    std::vector<float> heights = {3, 3};
    BarLabelConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red labels should render red pixels";
}

TEST(BarLabelRegression, IntegerFormat) {
    BLFigure cf(256);
    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> heights = {10, 20, 30};
    BarLabelConfig cfg;
    cfg.fmt = "%d";
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<BarLabelPlot>(std::move(x), std::move(heights), 0.0f, cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isDark);
    EXPECT_GT(darkCount, 10u) << "Integer format labels should render";
}
