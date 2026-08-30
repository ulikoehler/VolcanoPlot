// tests/test_errorbar3d.cpp — tests for Errorbar3D
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/Errorbar3D.hpp>
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

TEST(Errorbar3DRegression, BasicErrorbar3DRenders) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1, 2}, y = {0, 1, 2}, z = {0, 1, 2};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.2f, 0.2f, 0.2f};
    cfg.yerr = {0.2f, 0.2f, 0.2f};
    cfg.zerr = {0.3f, 0.3f, 0.3f};
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {1, 1, 1}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "3D errorbar should render";
}

TEST(Errorbar3DRegression, EmptyDataRendersNothing) {
    Fig3D cf(256);
    std::vector<float> x, y, z;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z));
    plot->setCamera(Camera3D{});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty 3D errorbar should render nothing";
}

TEST(Errorbar3DRegression, NoErrorsOnlyMarkers) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.markerColor = Color::blue();
    cfg.markerSize = 10.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{4, 4, 4}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar with no errors should render markers";
}

TEST(Errorbar3DRegression, XErrorsOnly) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 0}, z = {0, 0};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.3f, 0.3f};
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{4, -2, 3}, {0.5f, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar with x errors should render";
}

TEST(Errorbar3DRegression, YErrorsOnly) {
    Fig3D cf(256);
    std::vector<float> x = {0, 0}, y = {0, 1}, z = {0, 0};
    Errorbar3DConfig cfg;
    cfg.yerr = {0.3f, 0.3f};
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{-2, 4, 3}, {0, 0.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar with y errors should render";
}

TEST(Errorbar3DRegression, ZErrorsOnly) {
    Fig3D cf(256);
    std::vector<float> x = {0, 0}, y = {0, 0}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.zerr = {0.3f, 0.3f};
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{3, 3, 4}, {0, 0, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar with z errors should render";
}

TEST(Errorbar3DRegression, AsymmetricErrors) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.xerrLower = {0.1f, 0.2f};
    cfg.xerrUpper = {0.3f, 0.1f};
    cfg.zerrLower = {0.1f, 0.1f};
    cfg.zerrUpper = {0.4f, 0.4f};
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar with asymmetric errors should render";
}

TEST(Errorbar3DRegression, CustomColors) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.3f, 0.3f};
    cfg.zerr = {0.3f, 0.3f};
    cfg.markerColor = Color::red();
    cfg.errorbarColor = Color::green();
    cfg.markerSize = 10.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red markers should render";
}

TEST(Errorbar3DRegression, NoCaps) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.3f, 0.3f};
    cfg.zerr = {0.3f, 0.3f};
    cfg.drawCaps = false;
    cfg.markerColor = Color::blue();
    cfg.errorbarColor = Color::black();
    cfg.markerSize = 8.0f;
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "3D errorbar without caps should render";
}

TEST(Errorbar3DRegression, NoMarkers) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.3f, 0.3f};
    cfg.zerr = {0.3f, 0.3f};
    cfg.drawMarker = false;
    cfg.errorbarColor = Color::black();
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "3D errorbar without markers should render error bars";
}

TEST(Errorbar3DRegression, Autoscale) {
    Fig3D cf(256);
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    Errorbar3DConfig cfg;
    cfg.xerr = {0.3f, 0.3f};
    cfg.zerr = {0.5f, 0.5f};
    auto plot = std::make_unique<Errorbar3D>(std::move(x), std::move(y), std::move(z), cfg);
    plot->setCamera(Camera3D{{5, 5, 5}, {0.5f, 0.5f, 0.5f}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    // x: [0 - 0.3, 1 + 0.3] = [-0.3, 1.3]
    EXPECT_LE(av.x.min, 0.0f);
    EXPECT_GE(av.x.max, 1.0f);
    // z: [0 - 0.5, 1 + 0.5] = [-0.5, 1.5]
    EXPECT_LE(av.z.min, 0.0f);
    EXPECT_GE(av.z.max, 1.0f);
}
