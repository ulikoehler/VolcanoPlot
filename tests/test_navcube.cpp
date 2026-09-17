// tests/test_navcube.cpp — tests for NavCubePlot (3D orientation indicator)
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/NavCubePlot.hpp>
#include <volcano/plot/Transform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct Fig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit Fig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

size_t countIf(const Image& img, bool (*pred)(const Pixel&),
               int x0, int y0, int x1, int y1) {
    size_t n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (pred(img.get(uint32_t(x), uint32_t(y)))) ++n;
    return n;
}

bool isRed(const Pixel& p)   { return p.r > 120 && p.r > p.g + 60 && p.r > p.b + 60; }
bool isGreen(const Pixel& p) { return p.g > 100 && p.g > p.r + 40 && p.g > p.b + 40; }
bool isBlue(const Pixel& p)  { return p.b > 100 && p.b > p.r + 40 && p.b > p.g + 40; }
bool isNotWhite(const Pixel& p) { return !(p.r > 230 && p.g > 230 && p.b > 230); }
bool isGray(const Pixel& p) {
    return p.r > 80 && p.r < 220 &&
           std::abs(int(p.r) - int(p.g)) < 30 &&
           std::abs(int(p.g) - int(p.b)) < 30;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Triad mode
// ═══════════════════════════════════════════════════════════════════════════

TEST(NavCubeRegression, TriadRendersAllThreeAxes) {
    Fig cf(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{2, 2, 2}, {0, 0, 0}, {0, 0, 1}};
    cf.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    auto img = cf.render();

    // Upper-left corner region: size=36, margin=12 → within [0, ~96].
    size_t red   = countIf(img, isRed,   0, 0, 100, 100);
    size_t green = countIf(img, isGreen, 0, 0, 100, 100);
    size_t blue  = countIf(img, isBlue,  0, 0, 100, 100);
    EXPECT_GT(red, 3u)   << "X axis arrow should render red pixels";
    EXPECT_GT(green, 3u) << "Y axis arrow should render green pixels";
    EXPECT_GT(blue, 3u)  << "Z axis arrow should render blue pixels";
}

TEST(NavCubeRegression, TriadRespectsCorner) {
    Fig cf(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{2, 2, 2}, {0, 0, 0}, {0, 0, 1}};
    cfg.corner = NavCubeCorner::LowerRight;
    cf.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    auto img = cf.render();

    // Lower-right corner: pixels only in the bottom-right quadrant.
    size_t corner = countIf(img, isNotWhite, 156, 156, 256, 256);
    size_t elsewhere = countIf(img, isNotWhite, 0, 0, 150, 150);
    EXPECT_GT(corner, 10u) << "Triad should render in the lower-right corner";
    EXPECT_EQ(elsewhere, 0u) << "Triad should not render in upper-left";
}

TEST(NavCubeRegression, TopDownCameraPointsXRightYUp) {
    // Camera directly above looking down: +X → screen right, +Y → screen up.
    Fig cf(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{0, 0, 5}, {0, 0, 0}, {0, 1, 0}};
    cfg.showNegativeAxes = false;
    cfg.showLabels = false;
    cf.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    auto img = cf.render();

    // Center of indicator: margin+size = 48 → center ~(48, 48) px.
    // +X projects to screen right → red pixels at x > 48.
    size_t redRight = countIf(img, isRed, 55, 30, 90, 70);
    // +Y projects to screen up → green pixels at y < 48.
    size_t greenUp = countIf(img, isGreen, 30, 10, 70, 42);
    EXPECT_GT(redRight, 3u) << "+X axis should point screen-right";
    EXPECT_GT(greenUp, 3u) << "+Y axis should point screen-up";
}

TEST(NavCubeRegression, RotatedCameraChangesOrientation) {
    // Two different camera yaw angles should give different indicator images.
    Fig cf1(256), cf2(256);
    NavCubeConfig cfg1, cfg2;
    cfg1.camera = Camera3D{{5, 0, 2}, {0, 0, 0}, {0, 0, 1}};
    cfg2.camera = Camera3D{{0, 5, 2}, {0, 0, 0}, {0, 0, 1}};
    cf1.axes->addPlot(std::make_unique<NavCubePlot>(cfg1));
    cf2.axes->addPlot(std::make_unique<NavCubePlot>(cfg2));
    auto img1 = cf1.render();
    auto img2 = cf2.render();

    bool diff = false;
    for (uint32_t y = 0; y < 110 && !diff; ++y)
        for (uint32_t x = 0; x < 110 && !diff; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) diff = true;
    EXPECT_TRUE(diff) << "Different camera yaw should rotate the indicator";
}

TEST(NavCubeRegression, NegativeAxesToggle) {
    Fig cf1(256), cf2(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{2, 2, 2}, {0, 0, 0}, {0, 0, 1}};
    cfg.showLabels = false;
    NavCubeConfig cfgNo = cfg;
    cfgNo.showNegativeAxes = false;
    cf1.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    cf2.axes->addPlot(std::make_unique<NavCubePlot>(cfgNo));
    auto img1 = cf1.render();
    auto img2 = cf2.render();

    size_t n1 = countIf(img1, isNotWhite, 0, 0, 110, 110);
    size_t n2 = countIf(img2, isNotWhite, 0, 0, 110, 110);
    EXPECT_GT(n1, n2) << "Negative half-axes should add pixels";
}

// ═══════════════════════════════════════════════════════════════════════════
// Cube mode
// ═══════════════════════════════════════════════════════════════════════════

TEST(NavCubeRegression, CubeModeRendersEdges) {
    Fig cf(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{2, 2, 2}, {0, 0, 0}, {0, 0, 1}};
    cfg.mode = NavCubeMode::Cube;
    cfg.showLabels = false;
    cfg.showNegativeAxes = false;
    cf.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    auto img = cf.render();

    // Cube edges are gray; axis arrows still drawn.
    size_t gray  = countIf(img, isGray, 0, 0, 110, 110);
    size_t color = countIf(img, isNotWhite, 0, 0, 110, 110);
    EXPECT_GT(gray, 10u)  << "Cube wireframe should render gray edges";
    EXPECT_GT(color, 20u) << "Cube mode should still draw axis arrows";
}

TEST(NavCubeRegression, DoesNotAffectAutoscale) {
    Fig cf(256);
    cf.axes->addPlot(std::make_unique<NavCubePlot>());
    cf.render();
    const auto& vp = cf.axes->viewport();
    // Overlay must not expand the viewport beyond the default/autoscaled range.
    EXPECT_LE(vp.x.span(), 2.0f);
    EXPECT_LE(vp.y.span(), 2.0f);
}

TEST(NavCubeRegression, LabelsRender) {
    Fig cf(256);
    NavCubeConfig cfg;
    cfg.camera = Camera3D{{2, 2, 2}, {0, 0, 0}, {0, 0, 1}};
    cfg.showLabels = true;
    cfg.showNegativeAxes = false;
    cf.axes->addPlot(std::make_unique<NavCubePlot>(cfg));
    auto img = cf.render();

    // Labels are drawn in the axis colors — the "x"/"y"/"z" glyphs add
    // colored pixels beyond the arrow lines themselves. Just verify the
    // indicator region has a reasonable amount of colored ink.
    size_t color = countIf(img, isNotWhite, 0, 0, 110, 110);
    EXPECT_GT(color, 30u) << "Triad with labels should render text + arrows";
}
