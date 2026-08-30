// tests/test_bar3d.cpp — tests for Bar3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Bar3D.hpp>
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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(Bar3DRegression, BasicBar3DRenders) {
    Fig3D cf(256);
    // A single 3D bar at origin, 1x1x2 in size.
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> dx = {1}, dy = {1}, dz = {2};
    Bar3DConfig cfg;
    cfg.color = Color::blue();
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "3D bar should render filled faces";
}

TEST(Bar3DRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z, dx, dy, dz;
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D bar data should render nothing";
}

TEST(Bar3DRegression, MultipleBars3D) {
    Fig3D cf(256);
    // 4 bars in a 2x2 grid.
    std::vector<float> x = {0, 1, 0, 1};
    std::vector<float> y = {0, 0, 1, 1};
    std::vector<float> z = {0, 0, 0, 0};
    std::vector<float> dx = {0.8f, 0.8f, 0.8f, 0.8f};
    std::vector<float> dy = {0.8f, 0.8f, 0.8f, 0.8f};
    std::vector<float> dz = {1, 2, 3, 1.5f};
    Bar3DConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {1, 1, 1.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Multiple 3D bars should render";
}

TEST(Bar3DRegression, CustomColor) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> dx = {1}, dy = {1}, dz = {2};
    Bar3DConfig cfg;
    cfg.color = Color::red();
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 20 && p.r > p.b + 20) ++redCount;
        }
    EXPECT_GT(redCount, 50u) << "Red 3D bar should render red pixels";
}

TEST(Bar3DRegression, WithEdges) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> dx = {1}, dy = {1}, dz = {2};
    Bar3DConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    cfg.drawEdges = true;
    cfg.edgeColor = Color::black();
    cfg.edgeWidth = 2.0f;
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "3D bar with edges should render";
}

TEST(Bar3DRegression, WithoutEdges) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> dx = {1}, dy = {1}, dz = {2};
    Bar3DConfig cfg;
    cfg.color = Color::blue();
    cfg.drawEdges = false;
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "3D bar without edges should render";
}

TEST(Bar3DRegression, DifferentCameraAngles) {
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    std::vector<float> dx = {0.8f, 0.8f}, dy = {0.8f, 0.8f}, dz = {2, 1};

    Fig3D cf1(256);
    Bar3DConfig cfg1;
    cfg1.color = Color::blue();
    auto p1 = std::make_unique<Bar3D>(x, y, z, dx, dy, dz, cfg1);
    p1->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Bar3DConfig cfg2;
    cfg2.color = Color::blue();
    auto p2 = std::make_unique<Bar3D>(x, y, z, dx, dy, dz, cfg2);
    p2->setCamera(Camera3D{{-4, 4, 4}, {0.5f, 0.5f, 1.0f}, {0, 0, 1}});
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

TEST(Bar3DRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    std::vector<float> dx = {1, 1}, dy = {1, 1}, dz = {2, 3};
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz));
    plot->setCamera(Camera3D{{4, 4, 4}, {1, 0.5f, 1.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, 0.1f);
    EXPECT_GE(av.x.max, 1.9f);
    EXPECT_LE(av.y.min, 0.1f);
    EXPECT_GE(av.y.max, 0.9f);
    EXPECT_LE(av.z.min, 0.1f);
    EXPECT_GE(av.z.max, 2.9f);
}

TEST(Bar3DRegression, FlatBar) {
    Fig3D cf(256);
    // A flat bar (dz=0.1) — like a tile.
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> dx = {2}, dy = {2}, dz = {0.1f};
    Bar3DConfig cfg;
    cfg.color = Color::green();
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {1, 1, 0.05f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Flat 3D bar should render";
}

TEST(Bar3DRegression, ManyBars3D) {
    Fig3D cf(256);
    // 3x3 grid of bars.
    std::vector<float> x, y, z, dx, dy, dz;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            x.push_back(static_cast<float>(i));
            y.push_back(static_cast<float>(j));
            z.push_back(0);
            dx.push_back(0.8f);
            dy.push_back(0.8f);
            dz.push_back(static_cast<float>(i + j + 1));
        }
    Bar3DConfig cfg;
    cfg.color = Color::fromRgba8(31, 119, 180, 200);
    auto plot = std::make_unique<Bar3D>(std::move(x), std::move(y), std::move(z),
                                         std::move(dx), std::move(dy), std::move(dz), cfg);
    plot->setCamera(Camera3D{{6, 6, 6}, {1, 1, 2}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "Many 3D bars should render";
}
