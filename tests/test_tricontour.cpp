// tests/test_tricontour.cpp — tests for TricontourPlot and TricontourfPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/TricontourPlot.hpp>
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

struct SurfPts { std::vector<float> x, y, z; };

// Scattered points on a sine surface.
SurfPts makeSineSurface(int n) {
    SurfPts s;
    for (int i = 0; i < n; ++i) {
        float angle = static_cast<float>(i) / n * 2.0f * static_cast<float>(M_PI);
        float r = 3.0f * static_cast<float>(i) / n;
        float x = r * std::cos(angle);
        float y = r * std::sin(angle);
        float z = std::sin(std::sqrt(x*x + y*y));
        s.x.push_back(x);
        s.y.push_back(y);
        s.z.push_back(z);
    }
    return s;
}

// Asymmetric scattered points.
SurfPts makeAsymSurface(int n) {
    SurfPts s;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float x = -3.0f + 6.0f * i / (n - 1);
            float y = -3.0f + 6.0f * j / (n - 1);
            s.x.push_back(x);
            s.y.push_back(y);
            s.z.push_back(x * 0.3f + y * 0.5f);
        }
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TricontourPlot — contour lines on triangulated data
// ═══════════════════════════════════════════════════════════════════════════

TEST(TricontourRegression, BasicTricontourRenders) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.lineColor = Color::blue();
    cfg.lineWidth = 1.5f;
    auto plot = std::make_unique<TricontourPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Tricontour should render lines";
}

TEST(TricontourRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    auto plot = std::make_unique<TricontourPlot>(std::move(x), std::move(y), std::move(z));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty tricontour should render nothing";
}

TEST(TricontourRegression, ExplicitLevels) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.levels = {-0.5f, 0.0f, 0.5f};
    cfg.lineColor = Color::blue();
    auto plot = std::make_unique<TricontourPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Tricontour with explicit levels should render";
}

TEST(TricontourRegression, CustomColor) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.lineColor = Color::red();
    auto plot = std::make_unique<TricontourPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 3u) << "Red tricontour should render red pixels";
}

TEST(TricontourRegression, DifferentCameraAngles) {
    auto s = makeAsymSurface(8);

    Fig3D cf1(256);
    TricontourConfig cfg1;
    cfg1.lineColor = Color::blue();
    auto p1 = std::make_unique<TricontourPlot>(s.x, s.y, s.z, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    TricontourConfig cfg2;
    cfg2.lineColor = Color::blue();
    auto p2 = std::make_unique<TricontourPlot>(s.x, s.y, s.z, cfg2);
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

TEST(TricontourRegression, Autoscale) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    auto plot = std::make_unique<TricontourPlot>(std::move(s.x), std::move(s.y), std::move(s.z));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, 0.0f);
    EXPECT_GE(av.x.max, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TricontourfPlot — filled contour bands on triangulated data
// ═══════════════════════════════════════════════════════════════════════════

TEST(TricontourRegression, BasicTricontourfRenders) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TricontourfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Tricontourf should fill area";
}

TEST(TricontourRegression, TricontourfEmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    auto plot = std::make_unique<TricontourfPlot>(std::move(x), std::move(y), std::move(z));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty tricontourf should render nothing";
}

TEST(TricontourRegression, TricontourfCustomColormap) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.cmap = &colormaps::plasma();
    auto plot = std::make_unique<TricontourfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Custom colormap tricontourf should render";
}

TEST(TricontourRegression, TricontourfExplicitLevels) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    TricontourConfig cfg;
    cfg.levels = {-0.5f, 0.0f, 0.5f};
    cfg.cmap = &colormaps::viridis();
    auto plot = std::make_unique<TricontourfPlot>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "Tricontourf with explicit levels should render";
}

TEST(TricontourRegression, TricontourfDifferentCameraAngles) {
    auto s = makeAsymSurface(8);

    Fig3D cf1(256);
    TricontourConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    auto p1 = std::make_unique<TricontourfPlot>(s.x, s.y, s.z, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    TricontourConfig cfg2;
    cfg2.cmap = &colormaps::viridis();
    auto p2 = std::make_unique<TricontourfPlot>(s.x, s.y, s.z, cfg2);
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

TEST(TricontourRegression, TricontourfAutoscale) {
    Fig3D cf(256);
    auto s = makeSineSurface(30);
    auto plot = std::make_unique<TricontourfPlot>(std::move(s.x), std::move(s.y), std::move(s.z));
    plot->setCamera(Camera3D{{5, 5, 5}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, 0.0f);
    EXPECT_GE(av.x.max, 0.0f);
}
