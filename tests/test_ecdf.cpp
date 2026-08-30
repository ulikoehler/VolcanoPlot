// tests/test_ecdf.cpp — tests for ECDFPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ECDFPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct ECdfFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit ECdfFigure(uint32_t size = 256)
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

bool isBlue(const Pixel& p) {
    return p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20;
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

TEST(ECdfRegression, BasicECDFRenders) {
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "ECDF should render step function";
}

TEST(ECdfRegression, AutoscaleYRangeIsZeroToOne) {
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // Y range should include [0, 1] (with padding).
    EXPECT_LT(av.y.min, 0.5f);
    EXPECT_GT(av.y.max, 0.5f);
}

TEST(ECdfRegression, AutoscaleXRangeMatchesData) {
    ECdfFigure cf(256);
    std::vector<float> data{2, 4, 6, 8};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X range should span [2, 8] with padding.
    EXPECT_LT(av.x.min, 3.0f);
    EXPECT_GT(av.x.max, 7.0f);
}

TEST(ECdfRegression, StepFunctionReachesOne) {
    // With 5 uniform samples, the ECDF should reach 1.0 at the last value.
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    // Set viewport manually for predictable pixel positions.
    Viewport vp;
    vp.x = {0, 6}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    // At x=5 (last value), y should be 1.0 (top of plot).
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 5.0f, 1.0f);
    Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "ECDF should reach y=1 at the last data value";
}

TEST(ECdfRegression, StepFunctionStartsAtZero) {
    // Before the first value, ECDF should be 0.
    ECdfFigure cf(256);
    std::vector<float> data{3, 4, 5, 6, 7};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    Viewport vp;
    vp.x = {2, 8}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    // At x=3, y=0.1 (on the vertical step from 0 to 0.2).
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 3.0f, 0.1f);
    Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "ECDF should have a vertical step at x=3 starting from y=0";
}

TEST(ECdfRegression, UniformDataProducesSingleStep) {
    // All samples are the same → single step from 0 to 1.
    ECdfFigure cf(256);
    std::vector<float> data{5, 5, 5, 5, 5};
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Single-step ECDF should render";
    // Should be a vertical line at x=5, not many pixels.
    EXPECT_LT(filledCount, 500u) << "Single-step ECDF should be compact";
}

TEST(ECdfRegression, FillRenders) {
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    ECDFConfig cfg;
    cfg.fill = true;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t blueCount = countPixels(img, isBlue);
    EXPECT_GT(blueCount, 100u) << "Filled ECDF should have blue fill pixels";
}

TEST(ECdfRegression, ComplementaryCDF) {
    // Complementary CDF (survival function) starts at 1 and decreases to 0.
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5};
    ECDFConfig cfg;
    cfg.complementary = true;
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data), cfg));
    Viewport vp;
    vp.x = {0, 6}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    // At x=1 (first value), y=1.0 (S starts at 1 before first value).
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 1.0f, 1.0f);
    Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "Complementary CDF should start at y=1";
}

TEST(ECdfRegression, CustomColor) {
    ECdfFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5};
    ECDFConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 10u) << "Red ECDF should render red pixels";
}

TEST(ECdfRegression, RepeatedValuesHandledCorrectly) {
    // Repeated values should produce a larger jump.
    ECdfFigure cf(256);
    std::vector<float> data{1, 1, 1, 2, 3};  // 3 at x=1, 1 at x=2, 1 at x=3
    cf.axes->addPlot(std::make_unique<ECDFPlot>(std::move(data)));
    Viewport vp;
    vp.x = {0, 4}; vp.y = {0, 1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img = cf.render();

    // At x=1.5, y=0.6 (on the horizontal segment after the jump to 0.6).
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 1.5f, 0.6f);
    Pixel p = img.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "ECDF should be at 0.6 between x=1 and x=2 (3 out of 5 samples)";
}
