// tests/test_stem.cpp — tests for StemPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/StemPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct StemFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit StemFigure(uint32_t size = 256)
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

TEST(StemRegression, BasicStemRenders) {
    StemFigure cf(256);
    std::vector<float> y{1, 2, 3, 4, 5, 4, 3, 2, 1};
    StemConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Stem plot should render";
}

TEST(StemRegression, AutoscaleMatchesData) {
    StemFigure cf(256);
    std::vector<float> y{1, 3, 2, 5, 4};
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y)));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X: 0 to 4 (5 points)
    EXPECT_LT(av.x.min, 0.5f);
    EXPECT_GT(av.x.max, 3.5f);
    // Y: 0 to 5 (baseline=0, max=5)
    EXPECT_LT(av.y.min, 0.5f);
    EXPECT_GT(av.y.max, 4.5f);
}

TEST(StemRegression, StemsAreVertical) {
    StemFigure cf(256);
    std::vector<float> x{0, 1, 2, 3};
    std::vector<float> y{1, 2, 3, 4};
    StemConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {-0.5, 3.5}; vp.y = {0, 5}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Check that there are blue pixels at x=1 (second stem) at various heights.
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 1.0f, 0.0f);
    int ix = static_cast<int>(px);
    bool foundStem = false;
    for (uint32_t y = 0; y < img2.height(); ++y) {
        if (ix >= 0 && ix < static_cast<int>(img2.width()) && isBlue(img2.get(ix, y)))
            foundStem = true;
    }
    EXPECT_TRUE(foundStem) << "Stem at x=1 should have blue pixels (vertical line)";
}

TEST(StemRegression, MarkersAtTopOfStems) {
    StemFigure cf(256);
    std::vector<float> x{0, 1, 2};
    std::vector<float> y{1, 2, 3};
    StemConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cfg.markerSize = 10.0f;
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    Viewport vp;
    vp.x = {-0.5, 2.5}; vp.y = {0, 4}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Marker at (1, 2) should be visible.
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 1.0f, 2.0f);
    Pixel p = img2.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "Marker at top of stem (1,2) should be colored";
}

TEST(StemRegression, CustomBaseline) {
    StemFigure cf(256);
    std::vector<float> y{1, 2, 3, 4, 5};
    StemConfig cfg;
    cfg.baseline = 2.0f;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    // Y range should include baseline=2.
    const auto& av = cf.axes->viewport();
    EXPECT_LT(av.y.min, 2.5f);
    EXPECT_GT(av.y.max, 4.5f);
}

TEST(StemRegression, NegativeValuesRender) {
    StemFigure cf(256);
    std::vector<float> y{-1, -2, -3, -2, -1};
    StemConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Negative values should render below baseline";
}

TEST(StemRegression, NoMarkersOption) {
    StemFigure cf(256);
    std::vector<float> y{1, 2, 3, 4, 5};
    StemConfig cfg;
    cfg.markers = false;
    cfg.lineColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    // Should still render stems.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Stems should render without markers";
}

TEST(StemRegression, NoBaselineOption) {
    StemFigure cf(256);
    std::vector<float> y{1, 2, 3, 4, 5};
    StemConfig cfg;
    cfg.showBaseline = false;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    // Should still render stems and markers.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Stems should render without baseline";
}

TEST(StemRegression, CustomColors) {
    StemFigure cf(256);
    std::vector<float> y{1, 2, 3, 4, 5};
    StemConfig cfg;
    cfg.lineColor = Color::red();
    cfg.markerColor = Color::red();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(redCount, 20u) << "Red stem plot should render red pixels";
}

TEST(StemRegression, EmptyDataRendersNothing) {
    StemFigure cf(256);
    std::vector<float> y;
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty data should render nothing";
}

TEST(StemRegression, SinglePointRenders) {
    StemFigure cf(256);
    std::vector<float> y{5};
    StemConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.markerColor = Color::blue();
    cf.axes->addPlot(std::make_unique<StemPlot>(std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Single point should render a stem";
}
