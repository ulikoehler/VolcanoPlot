// tests/test_grid3d.cpp — tests for Grid3DRenderer
#include "PlotTestHarness.hpp"

#include <volcano/render/Grid3DRenderer.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Plot.hpp>

#include <gtest/gtest.h>

#include <cmath>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;
using namespace volcano::render;

namespace {

/// A test plot that draws a 3D grid using Grid3DRenderer.
/// This wraps the renderer as an IPlot so it can be added to a Figure.
class Grid3DTestPlot : public IPlot {
public:
    Grid3DTestPlot(Viewport viewport, Camera3D camera, Grid3DStyle style)
        : viewport_(viewport), camera_(camera), style_(style) {}

    void prepare(render::Renderer& r) override {
        auto& ctx = r.backend().context();
        grid_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache(),
                   ctx.allocator.handle(), ctx.device.graphicsQueue(),
                   ctx.graphicsPool.handle());
        prepared_ = true;
    }

    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override {
        if (!prepared_) return;
        vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}};
        grid_.draw(cmd, vrect, viewport_, camera_, style_);
    }

    void contributeToAutoscale(Viewport&) const override {}
    [[nodiscard]] std::string label() const override { return ""; }
    [[nodiscard]] Color legendColor() const override { return Color::black(); }

private:
    Viewport viewport_;
    Camera3D camera_;
    Grid3DStyle style_;
    Grid3DRenderer grid_;
    bool prepared_ = false;
};

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

/// Standard test camera: looks down at the floor from above at an angle.
Camera3D testCamera() {
    Camera3D cam{{5, 5, 10}, {0, 0, 0}, {0, 0, 1}};
    cam.aspect = 1.0f;
    return cam;
}

Viewport testViewport() {
    Viewport vp;
    vp.x = {-5, 5};
    vp.y = {-5, 5};
    vp.z = {-5, 5};
    return vp;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(Grid3DRegression, FloorGridRenders) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = true;
    style.backWallXY = false;
    style.sideWallYZ = false;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "3D floor grid should render lines";
}

TEST(Grid3DRegression, NoGridsRendersNothing) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.floorXZ = false;
    style.backWallXY = false;
    style.sideWallYZ = false;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "No grids enabled should render nothing";
}

TEST(Grid3DRegression, AllPlanesRender) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = true;
    style.backWallXY = true;
    style.sideWallYZ = true;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 30u) << "All planes should render more grid lines";
}

TEST(Grid3DRegression, BackWallOnly) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = false;
    style.backWallXY = true;
    style.sideWallYZ = false;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Back wall grid should render";
}

TEST(Grid3DRegression, SideWallOnly) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = false;
    style.backWallXY = false;
    style.sideWallYZ = true;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Side wall grid should render";
}

TEST(Grid3DRegression, CustomStep) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = true;
    style.backWallXY = false;
    style.sideWallYZ = false;
    style.step = 1.0f;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Custom step grid should render";
}

TEST(Grid3DRegression, DifferentCameraAngles) {
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = true;
    style.backWallXY = true;
    style.sideWallYZ = true;

    Fig3D cf1(256);
    auto p1 = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Camera3D cam2{{-5, 5, 10}, {0, 0, 0}, {0, 0, 1}};
    cam2.aspect = 1.0f;
    Fig3D cf2(256);
    auto p2 = std::make_unique<Grid3DTestPlot>(testViewport(), cam2, style);
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    bool anyDiff = false;
    for (uint32_t y = 64; y < 192; ++y)
        for (uint32_t x = 64; x < 192; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) {
                anyDiff = true;
                break;
            }
    EXPECT_TRUE(anyDiff) << "Different camera angles should produce different grid images";
}

TEST(Grid3DRegression, GridColorAffectsOutput) {
    Fig3D cf(256);
    Grid3DStyle style;
    style.color = Color::fromRgba8(255, 0, 0, 255);  // red
    style.floorXZ = true;
    style.backWallXY = false;
    style.sideWallYZ = false;
    auto plot = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 100 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 5u) << "Red grid should render red pixels";
}

TEST(Grid3DRegression, AutoStepComputesGrid) {
    Fig3D cf(256);
    Viewport vp;
    vp.x = {-50, 50};
    vp.y = {-50, 50};
    vp.z = {-50, 50};
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);
    style.floorXZ = true;
    style.backWallXY = false;
    style.sideWallYZ = false;
    style.step = 0.0f;  // auto
    Camera3D cam{{50, 50, 100}, {0, 0, 0}, {0, 0, 1}};
    cam.aspect = 1.0f;
    auto plot = std::make_unique<Grid3DTestPlot>(vp, cam, style);
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Auto-step grid should render";
}

TEST(Grid3DRegression, AllPlanesMoreThanFloorOnly) {
    Grid3DStyle style;
    style.color = Color::fromRgba8(100, 100, 100, 255);

    Fig3D cf1(256);
    Grid3DStyle style1 = style;
    style1.floorXZ = true;
    style1.backWallXY = false;
    style1.sideWallYZ = false;
    auto p1 = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style1);
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();
    size_t floorCount = countPixels(img1, isNotWhite);

    Fig3D cf2(256);
    Grid3DStyle style2 = style;
    style2.floorXZ = true;
    style2.backWallXY = true;
    style2.sideWallYZ = true;
    auto p2 = std::make_unique<Grid3DTestPlot>(testViewport(), testCamera(), style2);
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();
    size_t allCount = countPixels(img2, isNotWhite);

    EXPECT_GT(allCount, floorCount) << "All planes should render more than floor only";
}
