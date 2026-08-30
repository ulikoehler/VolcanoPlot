// tests/test_pcolorfast.cpp — tests for PcolorfastPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/PcolorfastPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct PCFFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit PCFFigure(uint32_t size = 256)
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
// Extent-based constructor (regular grid)
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolorfastRegression, ExtentBasicRenders) {
    PCFFigure cf(256);
    std::vector<float> C = {
        0, 1, 2,
        3, 4, 5,
        6, 7, 8
    };
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 3, 3, Range{0, 3}, Range{0, 3}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Extent-based pcolorfast should render";
}

TEST(PcolorfastRegression, ExtentAutoscale) {
    PCFFigure cf(256);
    std::vector<float> C(16, 0.5f);
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 4, 4, Range{1, 5}, Range{2, 6}));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, 0.8f, 0.1f);
    EXPECT_NEAR(av.x.max, 5.2f, 0.1f);
    EXPECT_NEAR(av.y.min, 1.8f, 0.1f);
    EXPECT_NEAR(av.y.max, 6.2f, 0.1f);
}

TEST(PcolorfastRegression, ExtentUniformGrid) {
    PCFFigure cf(256);
    // 2x2 grid with distinct values to check all cells render.
    std::vector<float> C = {0, 1, 2, 3};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 2, 2, Range{0, 2}, Range{0, 2}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Uniform grid should render all cells";
}

TEST(PcolorfastRegression, ExtentNonSquareGrid) {
    PCFFigure cf(256);
    // 4 cols, 2 rows
    std::vector<float> C = {0, 1, 2, 3, 4, 5, 6, 7};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 4, 2, Range{0, 4}, Range{0, 2}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Non-square grid should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Explicit edges constructor
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolorfastRegression, ExplicitEdgesBasicRenders) {
    PCFFigure cf(256);
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 2, 3};
    std::vector<float> C = {
        0, 1, 2,
        3, 4, 5,
        6, 7, 8
    };
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(x), std::move(y), std::move(C), 3, 3, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Explicit edges pcolorfast should render";
}

TEST(PcolorfastRegression, ExplicitEdgesNonUniform) {
    PCFFigure cf(256);
    // Non-uniform cell sizes.
    std::vector<float> x = {0, 0.5f, 2, 3};
    std::vector<float> y = {0, 1, 3};
    std::vector<float> C = {0, 1, 2, 3, 4, 5};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(x), std::move(y), std::move(C), 3, 2, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Non-uniform edges should render";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features
// ═══════════════════════════════════════════════════════════════════════════

TEST(PcolorfastRegression, CustomColormap) {
    PCFFigure cf(256);
    std::vector<float> C = {0, 1, 2, 3};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 2, 2, Range{0, 2}, Range{0, 2}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Custom colormap should render";
}

TEST(PcolorfastRegression, ExplicitValueRange) {
    PCFFigure cf(256);
    std::vector<float> C = {1, 2, 3, 4};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {0, 10};
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 2, 2, Range{0, 2}, Range{0, 2}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Explicit value range should render";
}

TEST(PcolorfastRegression, NaNSkipped) {
    // Compare: with NaN, fewer pixels than without.
    PCFFigure cf1(256);
    std::vector<float> C1 = {0, NAN, 2, 3};
    PcolorfastConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    cfg1.skipNaN = true;
    cf1.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C1), 2, 2, Range{0, 2}, Range{0, 2}, cfg1));
    auto imgNaN = cf1.render();
    size_t nanCount = countPixels(imgNaN, isNotWhite);

    PCFFigure cf2(256);
    std::vector<float> C2 = {0, 1, 2, 3};
    PcolorfastConfig cfg2;
    cfg2.cmap = &colormaps::viridis();
    cf2.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C2), 2, 2, Range{0, 2}, Range{0, 2}, cfg2));
    auto imgFull = cf2.render();
    size_t fullCount = countPixels(imgFull, isNotWhite);

    EXPECT_GT(nanCount, 100u) << "Non-NaN cells should render";
    EXPECT_LT(nanCount, fullCount) << "NaN cell should be skipped (fewer pixels)";
}

TEST(PcolorfastRegression, GradientRenders) {
    PCFFigure cf(256);
    // 8x8 gradient
    std::vector<float> C(64);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
            C[j * 8 + i] = static_cast<float>(i + j);
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 8, 8, Range{0, 8}, Range{0, 8}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "Gradient should fill most of the area";
}

TEST(PcolorfastRegression, SingleCellRenders) {
    PCFFigure cf(256);
    std::vector<float> C = {0.5f};
    PcolorfastConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C), 1, 1, Range{0, 1}, Range{0, 1}, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Single cell should render";
}

TEST(PcolorfastRegression, DifferentColormapsProduceDifferentColors) {
    PCFFigure cf1(256);
    std::vector<float> C1 = {0, 1, 2, 3};
    PcolorfastConfig cfg1;
    cfg1.cmap = &colormaps::viridis();
    cf1.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C1), 2, 2, Range{0, 2}, Range{0, 2}, cfg1));
    auto imgViridis = cf1.render();

    PCFFigure cf2(256);
    std::vector<float> C2 = {0, 1, 2, 3};
    PcolorfastConfig cfg2;
    cfg2.cmap = &colormaps::plasma();
    cf2.axes->addPlot(std::make_unique<PcolorfastPlot>(
        std::move(C2), 2, 2, Range{0, 2}, Range{0, 2}, cfg2));
    auto imgPlasma = cf2.render();

    // Center pixels should differ between colormaps.
    Pixel p1 = imgViridis.get(128, 128);
    Pixel p2 = imgPlasma.get(128, 128);
    EXPECT_FALSE(p1.approx(p2, 30))
        << "Different colormaps should produce different colors";
}
