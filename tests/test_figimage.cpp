// tests/test_figimage.cpp — tests for FigImagePlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/FigImagePlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct FIFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit FIFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }

    Image render() { return harness.render(figure); }
};

/// Pack RGBA8 into uint32_t (little-endian: R, G, B, A).
uint32_t packRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

bool isRed(const Pixel& p) {
    return p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30;
}

bool isGreen(const Pixel& p) {
    return p.g > 150 && p.g > p.r + 30 && p.g > p.b + 30;
}

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

TEST(FigImageRegression, BasicImageRenders) {
    FIFigure cf(256);
    // 4x4 red image
    std::vector<uint32_t> pixels(16, packRGBA(255, 0, 0));
    FigImageConfig cfg;
    cfg.x = 10;
    cfg.y = 10;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 4, 4, cfg));
    auto img = cf.render();

    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(redCount, 5u) << "FigImage should render red pixels";
}

TEST(FigImageRegression, ImageAtOrigin) {
    FIFigure cf(256);
    // 8x8 green image at (0, 0)
    std::vector<uint32_t> pixels(64, packRGBA(0, 255, 0));
    FigImageConfig cfg;
    cfg.x = 0;
    cfg.y = 0;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 8, 8, cfg));
    auto img = cf.render();

    // Top-left corner should be green (row 0 at top, col 0 at left).
    Pixel p = img.get(2, 2);
    EXPECT_TRUE(isGreen(p)) << "Top-left should be green (image at origin)";
}

TEST(FigImageRegression, ImageAtOffset) {
    FIFigure cf(256);
    // 4x4 red image at (100, 100)
    std::vector<uint32_t> pixels(16, packRGBA(255, 0, 0));
    FigImageConfig cfg;
    cfg.x = 100;
    cfg.y = 100;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 4, 4, cfg));
    auto img = cf.render();

    // Pixel at (102, 102) should be red (inside the image at offset 100,100).
    Pixel p = img.get(102, 102);
    EXPECT_TRUE(isRed(p)) << "Pixel at offset should be red";
}

TEST(FigImageRegression, ScaledImage) {
    FIFigure cf(256);
    // 2x2 green image scaled 10x → 20x20 pixels
    std::vector<uint32_t> pixels(4, packRGBA(0, 255, 0));
    FigImageConfig cfg;
    cfg.x = 50;
    cfg.y = 50;
    cfg.scale = 10.0f;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 2, 2, cfg));
    auto img = cf.render();

    size_t greenCount = countPixels(img, isGreen);
    EXPECT_GT(greenCount, 100u) << "Scaled image should cover more pixels";
}

TEST(FigImageRegression, TransparentPixelsSkipped) {
    FIFigure cf(256);
    // 4x4 image: half red, half transparent
    std::vector<uint32_t> pixels(16);
    for (int i = 0; i < 16; ++i) {
        if (i < 8) pixels[i] = packRGBA(255, 0, 0, 255);
        else pixels[i] = packRGBA(255, 0, 0, 0);  // transparent
    }
    FigImageConfig cfg;
    cfg.x = 10;
    cfg.y = 10;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 4, 4, cfg));
    auto img = cf.render();

    size_t redCount = countPixels(img, isRed);
    // Only half the pixels should be rendered (opaque ones).
    EXPECT_GT(redCount, 5u) << "Opaque pixels should render";
}

TEST(FigImageRegression, RowZeroAtTop) {
    FIFigure cf(256);
    // 2-row image: row 0 = red, row 1 = green
    std::vector<uint32_t> pixels = {
        packRGBA(255, 0, 0), packRGBA(255, 0, 0),  // row 0 (top) = red
        packRGBA(0, 255, 0), packRGBA(0, 255, 0)   // row 1 (bottom) = green
    };
    FigImageConfig cfg;
    cfg.x = 100;
    cfg.y = 100;
    cfg.scale = 10.0f;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 2, 2, cfg));
    auto img = cf.render();

    // Row 0 (top) should be red, row 1 (bottom) should be green.
    // In screen coords: y=100 is top row, y=110 is bottom row.
    Pixel top = img.get(105, 102);    // y=102 → near top of image
    Pixel bottom = img.get(105, 112); // y=112 → near bottom of image
    EXPECT_TRUE(isRed(top)) << "Row 0 (top) should be red";
    EXPECT_TRUE(isGreen(bottom)) << "Row 1 (bottom) should be green";
}

TEST(FigImageRegression, DoesNotAffectAutoscale) {
    FIFigure cf(256);
    std::vector<uint32_t> pixels(16, packRGBA(255, 0, 0));
    FigImageConfig cfg;
    cfg.x = 10;
    cfg.y = 10;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 4, 4, cfg));
    cf.render();

    // Viewport should remain at default (figimage doesn't contribute).
    const auto& av = cf.axes->viewport();
    // Default viewport is [0,1,0,1] with 5% padding.
    EXPECT_NEAR(av.x.min, -0.05f, 0.01f);
    EXPECT_NEAR(av.x.max, 1.05f, 0.01f);
}

TEST(FigImageRegression, MultiColorImage) {
    FIFigure cf(256);
    // 2x2 image with 4 different colors
    std::vector<uint32_t> pixels = {
        packRGBA(255, 0, 0),    packRGBA(0, 255, 0),
        packRGBA(0, 0, 255),    packRGBA(255, 255, 0)
    };
    FigImageConfig cfg;
    cfg.x = 50;
    cfg.y = 50;
    cfg.scale = 20.0f;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 2, 2, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Multi-color image should render";
}

TEST(FigImageRegression, SinglePixelImage) {
    FIFigure cf(256);
    std::vector<uint32_t> pixels = {packRGBA(255, 0, 0)};
    FigImageConfig cfg;
    cfg.x = 128;
    cfg.y = 128;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 1, 1, cfg));
    auto img = cf.render();

    size_t redCount = countPixels(img, isRed);
    EXPECT_GT(redCount, 0u) << "Single pixel image should render";
}

TEST(FigImageRegression, LargeImageFillsArea) {
    FIFigure cf(256);
    // 64x64 blue image at (0, 0)
    std::vector<uint32_t> pixels(64 * 64, packRGBA(0, 0, 255));
    FigImageConfig cfg;
    cfg.x = 0;
    cfg.y = 0;
    cf.axes->addPlot(std::make_unique<FigImagePlot>(std::move(pixels), 64, 64, cfg));
    auto img = cf.render();

    // Top-left area should be blue.
    Pixel p = img.get(10, 10);
    EXPECT_TRUE(p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20)
        << "Large image should fill area with blue";
}
