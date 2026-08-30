// tests/test_plot3d.cpp — tests for Plot3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Plot3D.hpp>
#include <volcano/plot/Transform.hpp>

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

// Generate a 3D helix: (cos(t), sin(t), t)
struct Helix { std::vector<float> x, y, z; };
Helix makeHelix(int n, float r = 1.0f, float h = 2.0f) {
    Helix hlx;
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / (n - 1) * h * static_cast<float>(M_PI);
        hlx.x.push_back(r * std::cos(t));
        hlx.y.push_back(r * std::sin(t));
        hlx.z.push_back(t * 0.3f);
    }
    return hlx;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(Plot3DRegression, BasicLine3DRenders) {
    Fig3D cf(256);
    auto h = makeHelix(50);
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(h.x), std::move(h.y), std::move(h.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}, 45.0f, 1.0f});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "3D line should render";
}

TEST(Plot3DRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D data should render nothing";
}

TEST(Plot3DRegression, SinglePointRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    // A single point can't form a line, so nothing should render.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Single point 3D line should render nothing";
}

TEST(Plot3DRegression, TwoPointsRendersLine) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{2, -2, 2}, {0.5f, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Two-point 3D line should render";
}

TEST(Plot3DRegression, CustomColor) {
    Fig3D cf(256);
    auto h = makeHelix(50);
    Plot3DConfig cfg;
    cfg.color = Color::red();
    auto plot = std::make_unique<Plot3D>(std::move(h.x), std::move(h.y), std::move(h.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red 3D line should render red pixels";
}

TEST(Plot3DRegression, WithMarkers) {
    Fig3D cf(256);
    auto h = makeHelix(20);
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    cfg.showMarkers = true;
    cfg.markerColor = Color::red();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Plot3D>(std::move(h.x), std::move(h.y), std::move(h.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "3D line with markers should render red markers";
}

TEST(Plot3DRegression, DifferentCameraAngles) {
    // The same helix viewed from different camera angles should produce
    // different projections (different pixel patterns).
    auto h = makeHelix(50);

    Fig3D cf1(256);
    auto h1 = h;
    Plot3DConfig cfg1;
    cfg1.color = Color::blue();
    auto plot1 = std::make_unique<Plot3D>(std::move(h1.x), std::move(h1.y), std::move(h1.z), cfg1);
    plot1->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(plot1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    auto h2 = h;
    Plot3DConfig cfg2;
    cfg2.color = Color::blue();
    auto plot2 = std::make_unique<Plot3D>(std::move(h2.x), std::move(h2.y), std::move(h2.z), cfg2);
    plot2->setCamera(Camera3D{{-3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(plot2));
    auto img2 = cf2.render();

    // Compare center pixels — different camera angles should produce different images.
    Pixel p1 = img1.get(128, 128);
    Pixel p2 = img2.get(128, 128);
    bool anyDiff = false;
    for (uint32_t y = 64; y < 192; ++y)
        for (uint32_t x = 64; x < 192; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) {
                anyDiff = true;
                break;
            }
    EXPECT_TRUE(anyDiff) << "Different camera angles should produce different images";
}

TEST(Plot3DRegression, Autoscale) {
    Fig3D cf(256);
    auto h = makeHelix(50, 1.0f, 2.0f);
    auto plot = std::make_unique<Plot3D>(std::move(h.x), std::move(h.y), std::move(h.z));
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    // x and y should be roughly [-1, 1] (helix radius).
    EXPECT_LE(av.x.min, -0.9f);
    EXPECT_GE(av.x.max, 0.9f);
    EXPECT_LE(av.y.min, -0.9f);
    EXPECT_GE(av.y.max, 0.9f);
}

TEST(Plot3DRegression, DiagonalLine3D) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    for (int i = 0; i < 20; ++i) {
        float t = static_cast<float>(i) / 19.0f;
        x.push_back(t);
        y.push_back(t);
        z.push_back(t);
    }
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(x), std::move(y), std::move(z), cfg);
    // Camera off-axis so the diagonal is not along the view direction.
    plot->setCamera(Camera3D{{3, -1, 2}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D diagonal line should render";
}

TEST(Plot3DRegression, ManyPoints3D) {
    Fig3D cf(256);
    auto h = makeHelix(200, 1.0f, 4.0f);
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(h.x), std::move(h.y), std::move(h.z), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0, 0, 1}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "3D line with many points should render";
}

TEST(Plot3DRegression, PointsBehindCameraSkipped) {
    Fig3D cf(256);
    std::vector<float> x = {0, 10}, y = {0, 10}, z = {0, 10};
    Plot3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Plot3D>(std::move(x), std::move(y), std::move(z), cfg);
    // Camera at origin looking at origin — points may be behind camera.
    plot->setCamera(Camera3D{{0.01f, 0.01f, 0.01f}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    // Should not crash, may or may not render depending on projection.
    SUCCEED();
}
