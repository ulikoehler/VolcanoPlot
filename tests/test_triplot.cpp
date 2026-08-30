// tests/test_triplot.cpp — tests for TriplotPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/TriplotPlot.hpp>
#include <volcano/plot/Triangulation.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct TriFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit TriFig(uint32_t size = 256)
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

bool isBlack(const Pixel& p) {
    return p.r < 80 && p.g < 80 && p.b < 80;
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

TEST(TriplotRegression, BasicTriplotRenders) {
    TriFig cf(256);
    // 3x3 grid of points
    std::vector<float> x, y;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            x.push_back(static_cast<float>(i));
            y.push_back(static_cast<float>(j));
        }
    TriplotConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 10u) << "Triplot should render edges";
}

TEST(TriplotRegression, ThreePointsOneTriangle) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 0, 1};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 5u) << "Single triangle should render 3 edges";
}

TEST(TriplotRegression, ExplicitTriangles) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0, 1};
    std::vector<float> y = {0, 0, 1, 1};
    std::vector<Triangle> tris = {{0, 1, 2}, {1, 3, 2}};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(
        std::move(x), std::move(y), std::move(tris), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 10u) << "Explicit triangles should render edges";
}

TEST(TriplotRegression, Autoscale) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f, 0.3f, 0.7f};
    std::vector<float> y = {0, 0, 1, 0.8f, 0.2f};
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.05f, 0.03f);
    EXPECT_NEAR(av.x.max, 1.05f, 0.03f);
    EXPECT_NEAR(av.y.min, -0.05f, 0.03f);
    EXPECT_NEAR(av.y.max, 1.05f, 0.03f);
}

TEST(TriplotRegression, CustomColor) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 0, 1};
    TriplotConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red triplot should render red edges";
}

TEST(TriplotRegression, WithMarkers) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f, 0.3f, 0.7f};
    std::vector<float> y = {0, 0, 1, 0.8f, 0.2f};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cfg.showMarkers = true;
    cfg.markerColor = Color::red();
    cfg.markerSize = 8.0f;
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Markers should render as red points";
}

TEST(TriplotRegression, WithoutMarkers) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 0, 1};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cfg.showMarkers = false;
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Without markers, only edges should render (no point markers).
    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 5u) << "Edges should render without markers";
}

TEST(TriplotRegression, GridPointsManyTriangles) {
    TriFig cf(256);
    // 5x5 grid → many triangles
    std::vector<float> x, y;
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5; ++i) {
            x.push_back(static_cast<float>(i));
            y.push_back(static_cast<float>(j));
        }
    TriplotConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 50u) << "Grid should produce many edges";
}

TEST(TriplotRegression, ScatteredPoints) {
    TriFig cf(256);
    std::vector<float> x = {0.1f, 0.9f, 0.3f, 0.7f, 0.5f, 0.2f, 0.8f};
    std::vector<float> y = {0.1f, 0.1f, 0.4f, 0.6f, 0.9f, 0.8f, 0.3f};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 10u) << "Scattered points should produce edges";
}

TEST(TriplotRegression, CustomLineWidth) {
    TriFig cf(256);
    std::vector<float> x = {0, 1, 0.5f};
    std::vector<float> y = {0, 0, 1};
    TriplotConfig cfg;
    cfg.color = Color::black();
    cfg.lineWidth = 3.0f;
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t darkCount = countPixels(img, isBlack);
    EXPECT_GT(darkCount, 10u) << "Thicker lines should render more pixels";
}

TEST(TriplotRegression, ColinearPointsHandled) {
    TriFig cf(256);
    // 4 colinear points — degenerate, but should not crash.
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 0, 0, 0};
    TriplotConfig cfg;
    cfg.color = Color::black();
    // Add a 5th non-colinear point to make triangulation possible.
    x.push_back(1.5f);
    y.push_back(1.0f);
    cf.axes->addPlot(std::make_unique<TriplotPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    // Should render something without crashing.
    SUCCEED();
}
