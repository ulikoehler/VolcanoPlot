// tests/test_matshow.cpp — tests for MatshowPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/MatshowPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct MatFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit MatFigure(uint32_t size = 256)
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

Viewport expectedViewport(float minX, float maxX, float minY, float maxY) {
    Viewport v;
    v.x = {minX, maxX};
    v.y = {minY, maxY};
    v.z = {0, 1};
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
    return v;
}

std::pair<float, float> dataToPixel(const Viewport& v, const Rect2D& rect,
                                    float dx, float dy) {
    float nx = (dx - v.x.min) / v.x.span();
    float ny = (dy - v.y.min) / v.y.span();
    float px = rect.x + nx * rect.width;
    float py = rect.y + (1.0f - ny) * rect.height;
    return {px, py};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(MatshowRegression, BasicMatshowRenders) {
    MatFigure cf(256);
    std::vector<float> data = {
        0, 1, 2,
        1, 2, 3,
        2, 3, 4
    };
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 3, 3, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Matshow should render colored cells";
}

TEST(MatshowRegression, AutoscaleMatchesMatrixDimensions) {
    MatFigure cf(256);
    std::vector<float> data = {
        0, 1,
        2, 3
    };
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 2));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.1f, 0.05f);
    EXPECT_NEAR(av.x.max, 2.1f, 0.05f);
    EXPECT_NEAR(av.y.min, -0.1f, 0.05f);
    EXPECT_NEAR(av.y.max, 2.1f, 0.05f);
}

TEST(MatshowRegression, RowZeroAtTop) {
    MatFigure cf(256);
    // Row 0 has value 0 (dark in viridis), row 1 has value 1 (bright).
    std::vector<float> data = {
        0, 0, 0,   // row 0 (top) — low values
        1, 1, 1    // row 1 (bottom) — high values
    };
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 3, cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 3}; vp.y = {0, 2}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Row 0 (top) maps to y=[1,2] → upper part of image.
    // Row 1 (bottom) maps to y=[0,1] → lower part.
    // Check that top and bottom have different colors (different values).
    auto [tx, ty] = dataToPixel(vp, cf.axes->rect, 1.5f, 1.5f);  // row 0 center
    auto [bx, by] = dataToPixel(vp, cf.axes->rect, 1.5f, 0.5f);  // row 1 center
    Pixel top = img2.get(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
    Pixel bottom = img2.get(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
    EXPECT_FALSE(top.approx(bottom, 30))
        << "Top and bottom rows should have different colors";
}

TEST(MatshowRegression, UniformMatrixUniformColor) {
    MatFigure cf(256);
    std::vector<float> data(16, 0.5f);
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 4, 4, cfg));
    auto img = cf.render();

    // All cells should have the same color (viridis at t=0.5).
    // Check center region uniformity.
    Pixel center = img.get(128, 128);
    Pixel offCenter = img.get(100, 100);
    EXPECT_TRUE(center.approx(offCenter, 30))
        << "Uniform matrix should have uniform color";
}

TEST(MatshowRegression, CustomColormap) {
    MatFigure cf(256);
    std::vector<float> data = {0, 1, 2, 3};
    MatshowConfig cfg;
    cfg.cmap = &colormaps::plasma();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 2, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Custom colormap should render";
}

TEST(MatshowRegression, ExplicitValueRange) {
    MatFigure cf(256);
    std::vector<float> data = {1, 2, 3, 4};
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cfg.valueRange = {0, 10};  // values 1-4 map to t=0.1-0.4
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 2, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Explicit value range should render";
}

TEST(MatshowRegression, NonRectangularMatrix) {
    MatFigure cf(256);
    // 2 rows, 3 cols
    std::vector<float> data = {
        0, 1, 2,
        3, 4, 5
    };
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 3, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 500u) << "Non-rectangular matrix should render";
}

TEST(MatshowRegression, SingleCellMatrix) {
    MatFigure cf(256);
    std::vector<float> data = {0.5f};
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 1, 1, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Single cell matrix should render";
}

TEST(MatshowRegression, GradientMatrix) {
    MatFigure cf(256);
    // 4x4 gradient: value = row * 4 + col
    std::vector<float> data(16);
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            data[j * 4 + i] = static_cast<float>(j * 4 + i);
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 4, 4, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 1000u) << "Gradient matrix should render";
}

TEST(MatshowRegression, NaNCellsSkipped) {
    MatFigure cf(256);
    std::vector<float> data = {
        0, NAN,
        1, 2
    };
    MatshowConfig cfg;
    cfg.cmap = &colormaps::viridis();
    cf.axes->addPlot(std::make_unique<MatshowPlot>(std::move(data), 2, 2, cfg));
    auto img = cf.render();

    // NaN cell should be skipped (transparent), so fewer pixels than
    // a full 2x2 matrix.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Non-NaN cells should render";
    // The NaN cell (top-right) should be white.
    Viewport vp;
    vp.x = {0, 2}; vp.y = {0, 2}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 1.5f, 1.5f);  // top-right
    Pixel p = img2.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    // Should be white or close to white (NaN cell skipped).
    EXPECT_TRUE(p.approx(Pixel::white(), 40))
        << "NaN cell should be white (skipped)";
}
