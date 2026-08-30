// tests/test_quiver3d.cpp — tests for Quiver3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Quiver3D.hpp>
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

TEST(Quiver3DRegression, BasicQuiver3DRenders) {
    Fig3D cf(256);
    // A few arrows pointing in various directions.
    std::vector<float> x = {0, 1, 0, 1}, y = {0, 0, 1, 1}, z = {0, 0, 0, 0};
    std::vector<float> u = {1, 0, 0.5f, -1}, v = {0, 1, 0.5f, 0}, w = {0, 0, 1, 0.5f};
    Quiver3DConfig cfg;
    cfg.color = Color::blue();
    cfg.scale = 0.5f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D quiver should render arrows";
}

TEST(Quiver3DRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z, u, v, w;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D quiver should render nothing";
}

TEST(Quiver3DRegression, SingleArrow) {
    Fig3D cf(256);
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> u = {1}, v = {0}, w = {0};
    Quiver3DConfig cfg;
    cfg.color = Color::red();
    cfg.scale = 1.0f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{3, 3, 3}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Single 3D arrow should render";
}

TEST(Quiver3DRegression, CustomColor) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    std::vector<float> u = {1, 0}, v = {0, 1}, w = {0, 0};
    Quiver3DConfig cfg;
    cfg.color = Color::red();
    cfg.scale = 0.5f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red 3D quiver should render red pixels";
}

TEST(Quiver3DRegression, WithoutFilledHeads) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1, 0, 1}, y = {0, 0, 1, 1}, z = {0, 0, 0, 0};
    std::vector<float> u = {1, 0, 0.5f, -1}, v = {0, 1, 0.5f, 0}, w = {0, 0, 1, 0.5f};
    Quiver3DConfig cfg;
    cfg.color = Color::blue();
    cfg.scale = 0.5f;
    cfg.filledHeads = false;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "3D quiver without filled heads should render shafts";
}

TEST(Quiver3DRegression, ScaleFactor) {
    // Different scale factors should produce different arrow lengths.
    std::vector<float> x = {0}, y = {0}, z = {0};
    std::vector<float> u = {1}, v = {0}, w = {0};

    Fig3D cf1(256);
    Quiver3DConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.scale = 0.5f;
    auto p1 = std::make_unique<Quiver3D>(x, y, z, u, v, w, cfg1);
    p1->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Quiver3DConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.scale = 2.0f;
    auto p2 = std::make_unique<Quiver3D>(x, y, z, u, v, w, cfg2);
    p2->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    size_t count1 = countPixels(img1, isNotWhite);
    size_t count2 = countPixels(img2, isNotWhite);
    EXPECT_NE(count1, count2) << "Different scale factors should produce different arrow sizes";
}

TEST(Quiver3DRegression, DifferentCameraAngles) {
    std::vector<float> x = {0, 1, 0, 1}, y = {0, 0, 1, 1}, z = {0, 0, 0, 0};
    std::vector<float> u = {1, 0, 0.5f, -1}, v = {0, 1, 0.5f, 0}, w = {0, 0, 1, 0.5f};

    Fig3D cf1(256);
    Quiver3DConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.scale = 0.5f;
    auto p1 = std::make_unique<Quiver3D>(x, y, z, u, v, w, cfg1);
    p1->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    Quiver3DConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.scale = 0.5f;
    auto p2 = std::make_unique<Quiver3D>(x, y, z, u, v, w, cfg2);
    p2->setCamera(Camera3D{{-5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
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

TEST(Quiver3DRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    std::vector<float> u = {1, 0}, v = {0, 1}, w = {0, 1};
    Quiver3DConfig cfg;
    cfg.scale = 1.0f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    // x: [0, 0+1] = [0, 1]
    EXPECT_LE(av.x.min, 0.0f);
    EXPECT_GE(av.x.max, 1.0f);
    // y: [0, 0+1] = [0, 1]
    EXPECT_LE(av.y.min, 0.0f);
    EXPECT_GE(av.y.max, 1.0f);
    // z: [0, 0+1] = [0, 1]
    EXPECT_LE(av.z.min, 0.0f);
    EXPECT_GE(av.z.max, 1.0f);
}

TEST(Quiver3DRegression, ManyArrows) {
    Fig3D cf(256);
    // 3x3x1 grid of arrows.
    std::vector<float> x, y, z, u, v, w;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            x.push_back(static_cast<float>(i));
            y.push_back(static_cast<float>(j));
            z.push_back(0);
            u.push_back(0.5f);
            v.push_back(0.5f);
            w.push_back(0.5f);
        }
    Quiver3DConfig cfg;
    cfg.color = Color::blue();
    cfg.scale = 0.5f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {1, 1, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Many 3D arrows should render";
}

TEST(Quiver3DRegression, VerticalArrows) {
    Fig3D cf(256);
    // Arrows pointing straight up (z direction).
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 0};
    std::vector<float> u = {0, 0}, v = {0, 0}, w = {1, 1};
    Quiver3DConfig cfg;
    cfg.color = Color::blue();
    cfg.scale = 1.0f;
    auto plot = std::make_unique<Quiver3D>(std::move(x), std::move(y), std::move(z),
                                            std::move(u), std::move(v), std::move(w), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Vertical 3D arrows should render";
}
