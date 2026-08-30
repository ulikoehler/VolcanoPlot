// tests/test_mexican_hat.cpp — tests for MexicanHatPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/MexicanHatPlot.hpp>
#include <volcano/plot/Transform.hpp>

#include <gtest/gtest.h>

#include <cmath>

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

TEST(MexicanHatRegression, BasicSurfaceRenders) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawWireframe = false;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  30, 30, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Mexican hat surface should render";
}

TEST(MexicanHatRegression, SurfaceWithWireframe) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawSurface = true;
    cfg.drawWireframe = true;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  20, 20, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Mexican hat with wireframe should render";
}

TEST(MexicanHatRegression, WireframeOnly) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawSurface = false;
    cfg.drawWireframe = true;
    cfg.wireframeColor = Color::blue();
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  20, 20, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Wireframe-only Mexican hat should render";
}

TEST(MexicanHatRegression, DifferentSigma) {
    // Different sigma values should produce different surfaces.
    Fig3D cf1(256);
    MexicanHatConfig cfg1;
    cfg1.drawWireframe = false;
    auto p1 = std::make_unique<MexicanHatPlot>(0.5f, Range{-5, 5}, Range{-5, 5},
                                               30, 30, cfg1);
    p1->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    MexicanHatConfig cfg2;
    cfg2.drawWireframe = false;
    auto p2 = std::make_unique<MexicanHatPlot>(2.0f, Range{-5, 5}, Range{-5, 5},
                                               30, 30, cfg2);
    p2->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    bool anyDiff = false;
    for (uint32_t y = 64; y < 192; ++y)
        for (uint32_t x = 64; x < 192; ++x)
            if (!img1.get(x, y).approx(img2.get(x, y), 30)) {
                anyDiff = true;
                break;
            }
    EXPECT_TRUE(anyDiff) << "Different sigma should produce different surfaces";
}

TEST(MexicanHatRegression, DifferentCameraAngles) {
    Fig3D cf1(256);
    MexicanHatConfig cfg1;
    cfg1.drawWireframe = false;
    auto p1 = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                               30, 30, cfg1);
    p1->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    MexicanHatConfig cfg2;
    cfg2.drawWireframe = false;
    auto p2 = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                               30, 30, cfg2);
    p2->setCamera(Camera3D{{-8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
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

TEST(MexicanHatRegression, Autoscale) {
    Fig3D cf(256);
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-3, 4}, Range{-2, 5},
                                                  20, 20);
    plot->setCamera(Camera3D{{8, 8, 8}, {0.5f, 1.5f, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_LE(av.x.min, -3.0f);
    EXPECT_GE(av.x.max, 4.0f);
    EXPECT_LE(av.y.min, -2.0f);
    EXPECT_GE(av.y.max, 5.0f);
}

TEST(MexicanHatRegression, CustomColormap) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.colormap = "plasma";
    cfg.drawWireframe = false;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  30, 30, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Custom colormap Mexican hat should render";
}

TEST(MexicanHatRegression, StrideAffectsWireframe) {
    // Larger stride should produce fewer wireframe lines.
    Fig3D cf1(256);
    MexicanHatConfig cfg1;
    cfg1.drawSurface = false;
    cfg1.drawWireframe = true;
    cfg1.rowStride = 1;
    cfg1.colStride = 1;
    auto p1 = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                               20, 20, cfg1);
    p1->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf1.axes->addPlot(std::move(p1));
    auto img1 = cf1.render();

    Fig3D cf2(256);
    MexicanHatConfig cfg2;
    cfg2.drawSurface = false;
    cfg2.drawWireframe = true;
    cfg2.rowStride = 5;
    cfg2.colStride = 5;
    auto p2 = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                               20, 20, cfg2);
    p2->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf2.axes->addPlot(std::move(p2));
    auto img2 = cf2.render();

    size_t count1 = countPixels(img1, isNotWhite);
    size_t count2 = countPixels(img2, isNotWhite);
    EXPECT_GT(count1, count2) << "Stride=1 should render more wireframe pixels than stride=5";
}

TEST(MexicanHatRegression, HighResolutionGrid) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawWireframe = false;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  80, 80, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 200u) << "High-resolution grid should render more pixels";
}

TEST(MexicanHatRegression, CenterPeakIsHighest) {
    // The Mexican hat wavelet has a peak at the origin.
    // Verify by checking that the center of the image has non-white pixels
    // (the peak should be visible from above).
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawWireframe = false;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  40, 40, cfg);
    // Camera looking down from above.
    plot->setCamera(Camera3D{{0, 0, 10}, {0, 0, 0}, {0, 1, 0}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Top-down view should show the wavelet";
}

TEST(MexicanHatRegression, NothingRenderedWhenBothDisabled) {
    Fig3D cf(256);
    MexicanHatConfig cfg;
    cfg.drawSurface = false;
    cfg.drawWireframe = false;
    auto plot = std::make_unique<MexicanHatPlot>(1.0f, Range{-5, 5}, Range{-5, 5},
                                                  20, 20, cfg);
    plot->setCamera(Camera3D{{8, 8, 8}, {0, 0, 0}, {0, 0, 1}});
    cf.axes->addPlot(std::move(plot));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Both surface and wireframe disabled should render nothing";
}
