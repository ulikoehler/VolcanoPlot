// tests/test_contour3d.cpp — tests for Contour3D and Contourf3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Contour3D.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Colormap.hpp>

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

// Asymmetric grid for camera angle tests.
Grid2D makeAsymGrid(uint32_t w, uint32_t h) {
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
            g.values[j * w + i] = x * 0.3f + y * 0.5f;
        }
    return g;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Contour3D — contour lines in 3D
// ═══════════════════════════════════════════════════════════════════════════

TEST(Contour3DRegression, BasicContour3DRenders) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.lineWidth = 1.5f;
    auto plot = std::make_unique<Contour3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "3D contour should render lines";
}

TEST(Contour3DRegression, EmptyGridRendersNothing) {
    Fig3D cf(256);
    Grid2D g;
    auto plot = std::make_unique<Contour3D>(std::move(g));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D contour should render nothing";
}

TEST(Contour3DRegression, ExplicitLevels) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.levels = {-0.5f, 0.0f, 0.5f};
    cfg.lineColor = Color::blue();
    auto plot = std::make_unique<Contour3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D contour with explicit levels should render";
}

TEST(Contour3DRegression, CustomColor) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.lineColor = Color::red();
    auto plot = std::make_unique<Contour3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red 3D contour should render red pixels";
}

TEST(Contour3DRegression, DifferentCameraAngles) {
    auto grid = makeAsymGrid(20, 20);

    Fig3D cf1(256);
    Contour3DConfig cfg1;
    cfg1.lineColor = Color::blue();
    auto p1 = std::make_unique<Contour3D>(grid, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Contour3DConfig cfg2;
    cfg2.lineColor = Color::blue();
    auto p2 = std::make_unique<Contour3D>(grid, cfg2);
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

TEST(Contour3DRegression, Autoscale) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    auto plot = std::make_unique<Contour3D>(std::move(grid));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -2.9f);
    EXPECT_GE(av.x.max, 2.9f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Contourf3D — filled contours in 3D
// ═══════════════════════════════════════════════════════════════════════════

TEST(Contour3DRegression, BasicContourf3DRenders) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<Contourf3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "3D contourf should fill area";
}

TEST(Contour3DRegression, Contourf3DEmptyGridRendersNothing) {
    Fig3D cf(256);
    Grid2D g;
    auto plot = std::make_unique<Contourf3D>(std::move(g));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D contourf should render nothing";
}

TEST(Contour3DRegression, Contourf3DCustomColormap) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.cmap = &colormaps::plasma();
    auto plot = std::make_unique<Contourf3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Custom colormap 3D contourf should render";
}

TEST(Contour3DRegression, Contourf3DExplicitLevels) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    Contour3DConfig cfg;
    cfg.levels = {-0.5f, 0.0f, 0.5f};
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<Contourf3D>(std::move(grid), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "3D contourf with explicit levels should render";
}

TEST(Contour3DRegression, Contourf3DDifferentCameraAngles) {
    auto grid = makeAsymGrid(20, 20);

    Fig3D cf1(256);
    Contour3DConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    auto p1 = std::make_unique<Contourf3D>(grid, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Contour3DConfig cfg2;
    cfg2.cmap = &colormaps::viridis();
    auto p2 = std::make_unique<Contourf3D>(grid, cfg2);
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

TEST(Contour3DRegression, Contourf3DAutoscale) {
    Fig3D cf(256);
    auto grid = makeSineGrid(20, 20);
    auto plot = std::make_unique<Contourf3D>(std::move(grid));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -2.9f);
    EXPECT_GE(av.x.max, 2.9f);
}
