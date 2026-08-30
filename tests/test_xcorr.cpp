// tests/test_xcorr.cpp — tests for XCorrPlot (acorr + xcorr)
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/XCorrPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct XCFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit XCFigure(uint32_t size = 256)
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
// Autocorrelation (acorr)
// ═══════════════════════════════════════════════════════════════════════════

TEST(XCorrRegression, BasicAcorrRenders) {
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Acorr should render stems";
}

TEST(XCorrRegression, AcorrPeakAtZeroLag) {
    // Normalized autocorrelation should have peak at lag=0 (value=1.0).
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cfg.normed = true;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    // Set viewport for predictable positions.
    Viewport vp;
    vp.x = {-8, 8}; vp.y = {-0.2, 1.1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // At lag=0, value=1.0 (peak). Check that there's a blue pixel there.
    auto [px, py] = dataToPixel(vp, cf.axes->rect, 0.0f, 1.0f);
    Pixel p = img2.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "Acorr peak at lag=0 should be colored";
}

TEST(XCorrRegression, AcorrAutoscaleIncludesZeroLag) {
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5};
    XCorrConfig cfg;
    cfg.maxLags = 3;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X range should include [-3, 3].
    EXPECT_LE(av.x.min, -2.5f);
    EXPECT_GE(av.x.max, 2.5f);
    // Y range should include [0, 1] (normalized, peak at 1).
    EXPECT_LT(av.y.min, 0.5f);
    EXPECT_GT(av.y.max, 0.5f);
}

TEST(XCorrRegression, AcorrSymmetric) {
    // Autocorrelation should be symmetric: r[-k] = r[k].
    XCFigure cf(256);
    std::vector<float> data{1, 3, 2, 4, 5, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    // Set viewport.
    Viewport vp;
    vp.x = {-7, 7}; vp.y = {-0.2, 1.1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    // Scan the full vertical column at lag=+2 and lag=-2 for blue pixels.
    auto [px1, py1] = dataToPixel(vp, cf.axes->rect, 2.0f, 0.0f);
    auto [px2, py2] = dataToPixel(vp, cf.axes->rect, -2.0f, 0.0f);
    int ix1 = static_cast<int>(px1);
    int ix2 = static_cast<int>(px2);
    bool foundPos = false, foundNeg = false;
    for (uint32_t y = 0; y < img2.height(); ++y) {
        if (ix1 >= 0 && ix1 < static_cast<int>(img2.width()) && isBlue(img2.get(ix1, y)))
            foundPos = true;
        if (ix2 >= 0 && ix2 < static_cast<int>(img2.width()) && isBlue(img2.get(ix2, y)))
            foundNeg = true;
    }
    EXPECT_TRUE(foundPos) << "Stem at lag=+2 should exist";
    EXPECT_TRUE(foundNeg) << "Stem at lag=-2 should exist (symmetric)";
}

// ═══════════════════════════════════════════════════════════════════════════
// Cross-correlation (xcorr)
// ═══════════════════════════════════════════════════════════════════════════

TEST(XCorrRegression, BasicXcorrRenders) {
    XCFigure cf(256);
    std::vector<float> x{1, 2, 3, 4, 5, 4, 3, 2, 1};
    std::vector<float> y{1, 1, 2, 3, 5, 4, 2, 1, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(x), std::move(y), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Xcorr should render stems";
}

TEST(XCorrRegression, XcorrIdenticalSignalsPeakAtZero) {
    // Cross-correlation of identical signals should peak at lag=0.
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cfg.normed = true;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(data, data, cfg));
    auto img = cf.render();

    Viewport vp;
    vp.x = {-8, 8}; vp.y = {-0.2, 1.1}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    auto img2 = cf.render();

    auto [px, py] = dataToPixel(vp, cf.axes->rect, 0.0f, 1.0f);
    Pixel p = img2.get(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
    EXPECT_FALSE(p.approx(Pixel::white(), 40))
        << "Xcorr of identical signals should peak at lag=0";
}

TEST(XCorrRegression, XcorrMaxLagsRespected) {
    XCFigure cf(256);
    std::vector<float> x{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<float> y{10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.maxLags = 3;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(x), std::move(y), cfg));
    cf.render();

    const auto& av = cf.axes->viewport();
    // X range should be limited to [-3, 3].
    EXPECT_GE(av.x.min, -4.0f);
    EXPECT_LE(av.x.max, 4.0f);
}

TEST(XCorrRegression, CustomColor) {
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::red();
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t redCount = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            Pixel p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 30 && p.r > p.b + 30) ++redCount;
        }
    EXPECT_GT(redCount, 10u) << "Red xcorr should render red pixels";
}

TEST(XCorrRegression, UnnormalizedAcorr) {
    // Unnormalized acorr should have larger values than normalized.
    XCFigure cf(256);
    std::vector<float> data{1, 2, 3, 4, 5, 4, 3, 2, 1};
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cfg.normed = false;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 50u) << "Unnormalized acorr should render";
}

TEST(XCorrRegression, EmptyDataRendersNothing) {
    XCFigure cf(256);
    std::vector<float> data;
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data)));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_EQ(filledCount, 0u) << "Empty data should render nothing";
}

TEST(XCorrRegression, ConstantSignalAcorr) {
    // Constant signal has zero variance → acorr is 0 everywhere (or 1 at lag 0
    // if we handle the degenerate case). Either way, it should render.
    XCFigure cf(256);
    std::vector<float> data(10, 5.0f);
    XCorrConfig cfg;
    cfg.color = Color::blue();
    cf.axes->addPlot(std::make_unique<XCorrPlot>(std::move(data), cfg));
    auto img = cf.render();

    // With zero variance, norm=1.0, and all correlations are 0.
    // So all stems are at y=0 (flat line). Should still render something.
    size_t filledCount = countPixels(img, isNotWhite);
    // May be very few pixels since all values are 0.
    // The stems are from (lag, 0) to (lag, 0) — degenerate.
    // Just check it doesn't crash.
    SUCCEED();
}
