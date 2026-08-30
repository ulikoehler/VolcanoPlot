// tests/test_scatter3d.cpp — tests for Scatter3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Scatter3D.hpp>
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

size_t countPixels(const Image& img, bool (*pred)(const Pixel&)) {
    size_t count = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x)
            if (pred(img.get(x, y))) ++count;
    return count;
}

// Generate 3D points on a sphere.
struct SpherePts { std::vector<float> x, y, z; };
SpherePts makeSphere(int n, float r = 1.0f) {
    SpherePts s;
    for (int i = 0; i < n; ++i) {
        float phi = static_cast<float>(i) / n * static_cast<float>(M_PI);
        for (int j = 0; j < n; ++j) {
            float theta = static_cast<float>(j) / n * 2.0f * static_cast<float>(M_PI);
            s.x.push_back(r * std::sin(phi) * std::cos(theta));
            s.y.push_back(r * std::sin(phi) * std::sin(theta));
            s.z.push_back(r * std::cos(phi));
        }
    }
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(Scatter3DRegression, BasicScatter3DRenders) {
    Fig3D cf(256);
    auto s = makeSphere(10);
    Scatter3DConfig cfg;
    cfg.color = Color::blue();
    cfg.size = 8.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "3D scatter should render";
}

TEST(Scatter3DRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    auto plot = std::make_unique<Scatter3D>(std::move(x), std::move(y), std::move(z));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D scatter should render nothing";
}

TEST(Scatter3DRegression, SinglePointRenders) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    Scatter3DConfig cfg;
    cfg.color = Color::blue();
    cfg.size = 10.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Single 3D point should render";
}

TEST(Scatter3DRegression, CustomColor) {
    Fig3D cf(256);
    auto s = makeSphere(8);
    Scatter3DConfig cfg;
    cfg.color = Color::red();
    cfg.size = 8.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red 3D scatter should render red pixels";
}

TEST(Scatter3DRegression, PerPointColors) {
    Fig3D cf(256);
    auto s = makeSphere(8);
    std::vector<Color> colors;
    for (size_t i = 0; i < s.x.size(); ++i) {
        colors.push_back(i % 2 == 0 ? Color::red() : Color::blue());
    }
    Scatter3DConfig cfg;
    cfg.size = 8.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z),
                                             std::move(colors), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0, blueCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
            if (p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20) ++blueCount;
        }
    EXPECT_GT(redCount + blueCount, 10u) << "Per-point colors should render";
}

TEST(Scatter3DRegression, PerPointSizes) {
    Fig3D cf(256);
    auto s = makeSphere(6);
    std::vector<Color> colors;
    std::vector<float> sizes;
    for (size_t i = 0; i < s.x.size(); ++i) {
        colors.push_back(Color::blue());
        sizes.push_back(4.0f + static_cast<float>(i % 3) * 6.0f);  // 4, 10, 16
    }
    Scatter3DConfig cfg;
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z),
                                             std::move(colors), std::move(sizes), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Per-point sizes should render";
}

TEST(Scatter3DRegression, DifferentCameraAngles) {
    auto s = makeSphere(10);

    Fig3D cf1(256);
    auto s1 = s;
    Scatter3DConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.size = 8.0f;
    auto plot1 = std::make_unique<Scatter3D>(std::move(s1.x), std::move(s1.y), std::move(s1.z), cfg1);
    plot1->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(plot1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    auto s2 = s;
    Scatter3DConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.size = 8.0f;
    auto plot2 = std::make_unique<Scatter3D>(std::move(s2.x), std::move(s2.y), std::move(s2.z), cfg2);
    plot2->setCamera(Camera3D{{-3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(plot2));
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

TEST(Scatter3DRegression, Autoscale) {
    Fig3D cf(256);
    auto s = makeSphere(10, 1.0f);
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z));
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -0.9f);
    EXPECT_GE(av.x.max, 0.9f);
    EXPECT_LE(av.y.min, -0.9f);
    EXPECT_GE(av.y.max, 0.9f);
    EXPECT_LE(av.z.min, -0.9f);
    EXPECT_GE(av.z.max, 0.9f);
}

TEST(Scatter3DRegression, ManyPoints3D) {
    Fig3D cf(256);
    auto s = makeSphere(20);
    Scatter3DConfig cfg;
    cfg.color = Color::blue();
    cfg.size = 4.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(s.x), std::move(s.y), std::move(s.z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "3D scatter with many points should render";
}

TEST(Scatter3DRegression, PointsBehindCameraSkipped) {
    Fig3D cf(256);
    std::vector<float> x = {0, 10}, y = {0, 10}, z = {0, 10};
    Scatter3DConfig cfg;
    cfg.color = Color::blue();
    cfg.size = 8.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{0.01f, 0.01f, 0.01f}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    // Should not crash.
    SUCCEED();
}

TEST(Scatter3DRegression, LargeMarkerSize) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    Scatter3DConfig cfg;
    cfg.color = Color::blue();
    cfg.size = 30.0f;
    auto plot = std::make_unique<Scatter3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Large marker should render many pixels";
}
