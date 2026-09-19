// tests/test_streamplot.cpp — tests for StreamPlot
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/StreamPlot.hpp>
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct StreamFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit StreamFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.grid().left = 0.0f; figure.grid().right = 1.0f;
        figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
    }

    Image render() { return harness.render(figure); }
};

bool isBlack(const Pixel& p) {
    return p.r < 50 && p.g < 50 && p.b < 50;
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

/// Build a uniform rightward flow field: u=1, v=0 everywhere.
std::pair<Grid2D, Grid2D> uniformRightward(uint32_t w, uint32_t h) {
    Grid2D u, v;
    u.width = w; u.height = h;
    u.xRange = {0, 10}; u.yRange = {0, 10};
    u.values.assign(w * h, 1.0f);
    v.width = w; v.height = h;
    v.xRange = {0, 10}; v.yRange = {0, 10};
    v.values.assign(w * h, 0.0f);
    return {u, v};
}

/// Build a rotational (circular) flow field around center (5,5).
std::pair<Grid2D, Grid2D> rotationalField(uint32_t w, uint32_t h) {
    Grid2D u, v;
    u.width = w; u.height = h;
    u.xRange = {0, 10}; u.yRange = {0, 10};
    v.width = w; v.height = h;
    v.xRange = {0, 10}; v.yRange = {0, 10};
    u.values.resize(w * h);
    v.values.resize(w * h);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i) {
            float x = u.xRange.min + float(i) * u.xRange.span() / (w - 1);
            float y = u.yRange.min + float(j) * u.yRange.span() / (h - 1);
            float dx = x - 5.0f, dy = y - 5.0f;
            // Rotational: u = -dy, v = dx (counterclockwise).
            u.values[j * w + i] = -dy;
            v.values[j * w + i] = dx;
        }
    return {u, v};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Basic rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST(StreamRegression, UniformRightwardFlowRenders) {
    StreamFigure cf(256);
    auto [u, v] = uniformRightward(20, 20);
    StreamConfig cfg;
    cfg.color = Color::black();
    cfg.lineWidth = 1.0f;
    cfg.density = 0.5f;
    cfg.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Streamplot should render black streamlines";
}

TEST(StreamRegression, RotationalFlowRenders) {
    StreamFigure cf(256);
    auto [u, v] = rotationalField(30, 30);
    StreamConfig cfg;
    cfg.color = Color::black();
    cfg.lineWidth = 1.0f;
    cfg.density = 0.5f;
    cfg.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Rotational streamplot should render";
}

TEST(StreamRegression, AutoscaleMatchesGrid) {
    StreamFigure cf(256);
    auto [u, v] = uniformRightward(10, 10);
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v)));
    cf.render();

    const auto& av = cf.axes->viewport();
    EXPECT_NEAR(av.x.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.x.max, 10.5f, 0.1f);
    EXPECT_NEAR(av.y.min, -0.5f, 0.1f);
    EXPECT_NEAR(av.y.max, 10.5f, 0.1f);
}

TEST(StreamRegression, HorizontalStreamlinesForRightwardFlow) {
    // For uniform rightward flow, streamlines should be roughly horizontal.
    StreamFigure cf(256);
    auto [u, v] = uniformRightward(20, 20);
    StreamConfig cfg;
    cfg.color = Color::black();
    cfg.lineWidth = 2.0f;
    cfg.density = 0.3f;
    cfg.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    // Check that there are black pixels spanning a wide x range at some y.
    // Find a row with many black pixels.
    int bestRow = -1;
    size_t bestCount = 0;
    for (uint32_t y = 20; y < 236; ++y) {
        size_t count = 0;
        for (uint32_t x = 0; x < 256; ++x)
            if (isBlack(img.get(x, y))) ++count;
        if (count > bestCount) { bestCount = count; bestRow = static_cast<int>(y); }
    }
    EXPECT_GT(bestCount, 20u) << "Should have a horizontal streamline spanning wide x range";
}

TEST(StreamRegression, ArrowsRender) {
    StreamFigure cf(256);
    auto [u, v] = uniformRightward(20, 20);
    StreamConfig cfg;
    cfg.color = Color::black();
    cfg.lineWidth = 1.0f;
    cfg.density = 0.5f;
    cfg.arrows = true;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    // With arrows, there should be filled triangles (black pixels).
    size_t blackCount = countPixels(img, isBlack);
    EXPECT_GT(blackCount, 50u) << "Streamplot with arrows should render";
}

TEST(StreamRegression, HigherDensityProducesMoreLines) {
    // Higher density should produce more streamline pixels.
    StreamFigure cf(256);
    auto [u1, v1] = uniformRightward(20, 20);
    StreamConfig cfg1;
    cfg1.color = Color::black();
    cfg1.lineWidth = 1.0f;
    cfg1.density = 0.2f;
    cfg1.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u1), std::move(v1), cfg1));
    auto imgLow = cf.render();
    size_t lowCount = countPixels(imgLow, isBlack);

    StreamFigure cf2(256);
    auto [u2, v2] = uniformRightward(20, 20);
    StreamConfig cfg2;
    cfg2.color = Color::black();
    cfg2.lineWidth = 1.0f;
    cfg2.density = 1.0f;
    cfg2.arrows = false;
    cf2.axes->addPlot(std::make_unique<StreamPlot>(std::move(u2), std::move(v2), cfg2));
    auto imgHigh = cf2.render();
    size_t highCount = countPixels(imgHigh, isBlack);

    EXPECT_GT(highCount, lowCount)
        << "Higher density should produce more streamline pixels";
}

TEST(StreamRegression, CustomColor) {
    StreamFigure cf(256);
    auto [u, v] = uniformRightward(20, 20);
    StreamConfig cfg;
    cfg.color = Color::red();
    cfg.lineWidth = 2.0f;
    cfg.density = 0.5f;
    cfg.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 20u) << "Red streamlines should render red pixels";
}

TEST(StreamRegression, ZeroFieldProducesNoLines) {
    // A zero vector field should produce no streamlines.
    StreamFigure cf(256);
    Grid2D u, v;
    u.width = 10; u.height = 10;
    u.xRange = {0, 10}; u.yRange = {0, 10};
    u.values.assign(100, 0.0f);
    v = u;
    StreamConfig cfg;
    cfg.color = Color::black();
    cfg.arrows = false;
    cf.axes->addPlot(std::make_unique<StreamPlot>(std::move(u), std::move(v), cfg));
    auto img = cf.render();

    size_t blackCount = countPixels(img, isBlack);
    EXPECT_EQ(blackCount, 0u) << "Zero field should produce no streamlines";
}

TEST(StreamRegression, ArrowsizeScalesArrowheads) {
    // mpl `arrowsize` scales arrowhead length/width — bigger arrows mean
    // more black pixels.
    auto renderWith = [](float arrowsize) {
        StreamFigure cf(256);
        auto [gu, gv] = uniformRightward(16, 16);
        StreamConfig cfg;
        cfg.density = 0.5f;
        cfg.arrowsize = arrowsize;
        cf.axes->addPlot(std::make_unique<StreamPlot>(gu, gv, cfg));
        return cf.render();
    };
    auto small = renderWith(0.3f);
    auto big = renderWith(3.0f);
    size_t smallBlack = countPixels(small, isBlack);
    size_t bigBlack = countPixels(big, isBlack);
    EXPECT_GT(smallBlack, 0u);
    EXPECT_GT(bigBlack, smallBlack)
        << "arrowsize=3 should produce more pixels than arrowsize=0.3";
}

TEST(StreamRegression, BrokenStreamlinesFalseCoastsThroughHole) {
    // Rightward field with a masked (NaN) hole in the middle columns.
    // With brokenStreamlines=false, lines coast through the hole and
    // produce more covered pixels than with the default true.
    auto field = [](uint32_t w, uint32_t h) {
        auto [u, v] = uniformRightward(w, h);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (uint32_t j = 0; j < h; ++j)
            for (uint32_t i = w / 2 - 3; i <= w / 2 + 3; ++i)
                u.values[j * w + i] = nan;
        return std::pair{u, v};
    };
    auto renderWith = [&](bool broken) {
        StreamFigure cf(256);
        auto [gu, gv] = field(16, 16);
        StreamConfig cfg;
        cfg.density = 0.5f;
        cfg.arrows = false;
        cfg.brokenStreamlines = broken;
        cf.axes->addPlot(std::make_unique<StreamPlot>(gu, gv, cfg));
        return cf.render();
    };
    auto broken = renderWith(true);
    auto coasted = renderWith(false);

    // The hole spans columns 5..10 of 16 → data x ≈ 3.3..6.7 → pixel
    // x ≈ 84..170. Broken lines stop at the hole edge (no black inside);
    // coasted lines cross it.
    auto holeBlack = [](const Image& img) {
        size_t n = 0;
        for (uint32_t y = 0; y < img.height(); ++y)
            for (uint32_t x = 90; x < 165; ++x)
                if (isBlack(img.get(x, y))) ++n;
        return n;
    };
    EXPECT_GT(holeBlack(coasted), holeBlack(broken))
        << "unbroken streamlines should cross the zero-field hole";
}
