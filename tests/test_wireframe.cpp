// tests/test_wireframe.cpp — tests for WireframePlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/WireframePlot.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/DataSeries.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct Fig3D {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit Fig3D(uint32_t size = 256)
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

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

// Generate a sine wave surface grid.
Grid2D makeSineGrid(uint32_t w, uint32_t h) {
    Grid2D g;
    g.width = w;
    g.height = h;
    g.xRange = {-3, 3};
    g.yRange = {-3, 3};
    g.values.resize(w * h);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i) {
            float x = -3.0f + 6.0f * i / (w - 1);
            float y = -3.0f + 6.0f * j / (h - 1);
            float r = std::sqrt(x*x + y*y);
            g.values[j * w + i] = std::sin(r);
        }
    return g;
}

// Generate a flat plane grid (z = 0).
Grid2D makeFlatGrid(uint32_t w, uint32_t h) {
    Grid2D g;
    g.width = w;
    g.height = h;
    g.xRange = {0, 1};
    g.yRange = {0, 1};
    g.values.resize(w * h, 0.0f);
    return g;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(WireframeRegression, BasicWireframeRenders) {
    Fig3D cf(256);
    auto grid = makeSineGrid(10, 10);
    WireframeConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Wireframe should render lines";
}

TEST(WireframeRegression, FlatPlaneRenders) {
    Fig3D cf(256);
    auto grid = makeFlatGrid(5, 5);
    WireframeConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Flat wireframe should render";
}

TEST(WireframeRegression, CustomColor) {
    Fig3D cf(256);
    auto grid = makeSineGrid(10, 10);
    WireframeConfig cfg;
    cfg.color = Color::red();
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 10u) << "Red wireframe should render red pixels";
}

TEST(WireframeRegression, RowStride) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    WireframeConfig cfg;
    cfg.color = Color::blue();
    cfg.rowStride = 2;  // only every other row
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Wireframe with row stride should render";
}

TEST(WireframeRegression, ColStride) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    WireframeConfig cfg;
    cfg.color = Color::blue();
    cfg.colStride = 3;  // only every third column
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Wireframe with col stride should render";
}

TEST(WireframeRegression, DifferentCameraAngles) {
    // Use an asymmetric surface so different angles produce different images.
    Grid2D grid;
    grid.width = 10;
    grid.height = 10;
    grid.xRange = {-3, 3};
    grid.yRange = {-3, 3};
    grid.values.resize(grid.width * grid.height);
    for (uint32_t j = 0; j < grid.height; ++j)
        for (uint32_t i = 0; i < grid.width; ++i) {
            float x = -3.0f + 6.0f * i / (grid.width - 1);
            float y = -3.0f + 6.0f * j / (grid.height - 1);
            grid.values[j * grid.width + i] = x * 0.3f + y * 0.5f;
        }

    Fig3D cf1(256);
    WireframeConfig cfg1;
    cfg1.color = Color::blue();
    auto p1 = std::make_unique<WireframePlot>(grid, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    WireframeConfig cfg2;
    cfg2.color = Color::blue();
    auto p2 = std::make_unique<WireframePlot>(grid, cfg2);
    p2->setCamera(Camera3D{{-5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    bool anyDiff = false;
    for (uint32_t y = 64; y < 192; ++y)
        for (uint32_t x = 64; x < 192; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) {
                anyDiff = true;
                break;
            }
    EXPECT_TRUE(anyDiff) << "Different camera angles should produce different images";
}

TEST(WireframeRegression, Autoscale) {
    Fig3D cf(256);
    auto grid = makeSineGrid(10, 10);
    auto plot = std::make_unique<WireframePlot>(grid);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -2.9f);
    EXPECT_GE(av.x.max, 2.9f);
    EXPECT_LE(av.y.min, -2.9f);
    EXPECT_GE(av.y.max, 2.9f);
    // z range: sin(r) is in [-1, 1]
    EXPECT_LE(av.z.min, -0.85f);
    EXPECT_GE(av.z.max, 0.85f);
}

TEST(WireframeRegression, SmallGrid) {
    Fig3D cf(256);
    auto grid = makeFlatGrid(2, 2);  // minimal 2x2 grid
    WireframeConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "2x2 wireframe should render";
}

TEST(WireframeRegression, LargeGrid) {
    Fig3D cf(256);
    auto grid = makeSineGrid(30, 30);
    WireframeConfig cfg;
    cfg.color = Color::blue();
    cfg.lineWidth = 0.5f;
    auto plot = std::make_unique<WireframePlot>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{6, 6, 6}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Large wireframe should render many pixels";
}

TEST(WireframeRegression, DomeSurface) {
    Fig3D cf(256);
    // Dome: z = sqrt(max(0, 1 - r^2))
    Grid2D g;
    g.width = 15;
    g.height = 15;
    g.xRange = {-1.5f, 1.5f};
    g.yRange = {-1.5f, 1.5f};
    g.values.resize(g.width * g.height);
    for (uint32_t j = 0; j < g.height; ++j)
        for (uint32_t i = 0; i < g.width; ++i) {
            float x = -1.5f + 3.0f * i / (g.width - 1);
            float y = -1.5f + 3.0f * j / (g.height - 1);
            float r2 = x*x + y*y;
            g.values[j * g.width + i] = r2 < 1.0f ? std::sqrt(1.0f - r2) : 0.0f;
        }
    WireframeConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<WireframePlot>(std::move(g), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Dome wireframe should render";
}
