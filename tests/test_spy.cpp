// tests/test_spy.cpp — tests for SpyPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/SpyPlot.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct SpyFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit SpyFigure(uint32_t size = 256)
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

bool isBlue(const Pixel& p) {
    return p.b > 100 && p.b > p.r + 30 && p.b > p.g + 20;
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

TEST(SpyRegression, BasicSpyRenders) {
    SpyFigure cf(256);
    // 4x4 identity matrix
    std::vector<float> data = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    SpyConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 4, 4, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 100u) << "Spy should render non-zero cells";
}

TEST(SpyRegression, IdentityMatrixHasDiagonal) {
    SpyFigure cf(256);
    std::vector<float> data = {
        1, 0, 0,
        0, 1, 0,
        0, 0, 1
    };
    SpyConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 3, 3, cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {0, 3}; vp.y = {0, 3}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Row 0 is at top (y=2 to y=3), col 0 is at left (x=0 to x=1).
    // So (0,0) element is at top-left, (1,1) at center, (2,2) at bottom-right.
    auto [tlx, tly] = dataToPixel(vp, cf.axes->rect, 0.5f, 2.5f);
    auto [cx, cy] = dataToPixel(vp, cf.axes->rect, 1.5f, 1.5f);
    auto [brx, bry] = dataToPixel(vp, cf.axes->rect, 2.5f, 0.5f);

    EXPECT_TRUE(isBlue(img2.get(static_cast<uint32_t>(tlx), static_cast<uint32_t>(tly))))
        << "Top-left (0,0) should be blue";
    EXPECT_TRUE(isBlue(img2.get(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy))))
        << "Center (1,1) should be blue";
    EXPECT_TRUE(isBlue(img2.get(static_cast<uint32_t>(brx), static_cast<uint32_t>(bry))))
        << "Bottom-right (2,2) should be blue";

    // Off-diagonal should be white.
    auto [ox, oy] = dataToPixel(vp, cf.axes->rect, 2.5f, 2.5f);
    EXPECT_FALSE(isBlue(img2.get(static_cast<uint32_t>(ox), static_cast<uint32_t>(oy))))
        << "Top-right (0,2) should be white (zero element)";
}

TEST(SpyRegression, AllZeroMatrixRendersNothing) {
    SpyFigure cf(256);
    std::vector<float> data(16, 0.0f);
    SpyConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 4, 4, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "All-zero matrix should render nothing";
}

TEST(SpyRegression, AllNonZeroMatrixFills) {
    SpyFigure cf(256);
    std::vector<float> data(16, 1.0f);
    SpyConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 4, 4, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "All-non-zero matrix should fill the area";
}

TEST(SpyRegression, AutoscaleMatchesMatrixDimensions) {
    SpyFigure cf(256);
    std::vector<float> data = {
        1, 0,
        0, 1
    };
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 2, 2));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.1f, 0.05f);
    EXPECT_NEAR(av.x.max, 2.1f, 0.05f);
    EXPECT_NEAR(av.y.min, -0.1f, 0.05f);
    EXPECT_NEAR(av.y.max, 2.1f, 0.05f);
}

TEST(SpyRegression, PrecisionThreshold) {
    // Values below precision should be treated as zero.
    // Compare: with precision, only 2 cells render; without, all 4 render.
    SpyFigure cf1(256);
    std::vector<float> data1 = {
        1.0f, 0.001f,
        0.001f, 1.0f
    };
    SpyConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.precision = 0.01f;  // 0.001 < 0.01 → treated as zero
    cf1.axes->addPlot(std::make_unique<SpyPlot>(std::move(data1), 2, 2, cfg1));
    auto imgFiltered = cf1.render();
    size_t filteredCount = countPixels(imgFiltered, isNotWhite);

    SpyFigure cf2(256);
    std::vector<float> data2 = {
        1.0f, 0.001f,
        0.001f, 1.0f
    };
    SpyConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.precision = 0.0f;  // no filtering
    cf2.axes->addPlot(std::make_unique<SpyPlot>(std::move(data2), 2, 2, cfg2));
    auto imgAll = cf2.render();
    size_t allCount = countPixels(imgAll, isNotWhite);

    EXPECT_GT(filteredCount, 100u) << "Diagonal should render with precision";
    EXPECT_LT(filteredCount, allCount)
        << "Precision filtering should render fewer cells";
}

TEST(SpyRegression, MarkerSizeControlsCellFill) {
    // Smaller markerSize → fewer filled pixels.
    SpyFigure cf1(256);
    std::vector<float> data1(16, 1.0f);
    SpyConfig cfg1;
    cfg1.color = Color::blue();
    cfg1.markerSize = 1.0f;
    cf1.axes->addPlot(std::make_unique<SpyPlot>(std::move(data1), 4, 4, cfg1));
    auto imgFull = cf1.render();
    size_t fullCount = countPixels(imgFull, isNotWhite);

    SpyFigure cf2(256);
    std::vector<float> data2(16, 1.0f);
    SpyConfig cfg2;
    cfg2.color = Color::blue();
    cfg2.markerSize = 0.5f;
    cf2.axes->addPlot(std::make_unique<SpyPlot>(std::move(data2), 4, 4, cfg2));
    auto imgHalf = cf2.render();
    size_t halfCount = countPixels(imgHalf, isNotWhite);

    EXPECT_GT(fullCount, halfCount)
        << "Full markerSize should have more pixels than half";
}

TEST(SpyRegression, CustomColor) {
    SpyFigure cf(256);
    std::vector<float> data = {1, 0, 0, 1};
    SpyConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 2, 2, cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 50u) << "Red spy should render red pixels";
}

TEST(SpyRegression, NonRectangularMatrix) {
    SpyFigure cf(256);
    // 2x3 matrix (2 rows, 3 cols)
    std::vector<float> data = {
        1, 0, 1,
        0, 1, 0
    };
    SpyConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 2, 3, cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Non-rectangular matrix should render";
}

TEST(SpyRegression, ZeroColorRendersZeroCells) {
    SpyFigure cf(256);
    std::vector<float> data = {
        1, 0,
        0, 1
    };
    SpyConfig cfg;
    cfg.color = Color::blue();
    cfg.zeroColor = Color::fromRgba8(200, 200, 200, 255);  // light gray
    cf.axes->addPlot(std::make_unique<SpyPlot>(std::move(data), 2, 2, cfg));
    auto img = cf.render();

    // Both zero and non-zero cells should be rendered.
    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5000u) << "With zeroColor, all cells should render";
}
