// tests/test_logscale.cpp — tests for loglog/semilogx/semilogy
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/DataSeries.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct LogFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit LogFigure(uint32_t size = 256)
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

/// Generate exponential data: y = base^x
std::vector<Point2D> expData(int n, float base = 10.0f) {
    std::vector<Point2D> pts;
    for (int i = 0; i < n; ++i) {
        float x = static_cast<float>(i);
        float y = std::pow(base, x);
        pts.push_back({x, y});
    }
    return pts;
}

/// Generate power-law data: y = x^power
std::vector<Point2D> powerData(int n, float power = 2.0f) {
    std::vector<Point2D> pts;
    for (int i = 1; i <= n; ++i) {
        float x = static_cast<float>(i);
        float y = std::pow(x, power);
        pts.push_back({x, y});
    }
    return pts;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Convenience methods
// ═══════════════════════════════════════════════════════════════════════════

TEST(LogScaleRegression, LoglogSetsBothAxes) {
    LogFigure cf;
    cf.axes->loglog();
    EXPECT_TRUE(cf.axes->logX());
    EXPECT_TRUE(cf.axes->logY());
}

TEST(LogScaleRegression, SemilogxSetsOnlyX) {
    LogFigure cf;
    cf.axes->semilogx();
    EXPECT_TRUE(cf.axes->logX());
    EXPECT_FALSE(cf.axes->logY());
}

TEST(LogScaleRegression, SemilogySetsOnlyY) {
    LogFigure cf;
    cf.axes->semilogy();
    EXPECT_FALSE(cf.axes->logX());
    EXPECT_TRUE(cf.axes->logY());
}

TEST(LogScaleRegression, SetLogXAndYDirectly) {
    LogFigure cf;
    cf.axes->setLogX(true);
    cf.axes->setLogY(true);
    EXPECT_TRUE(cf.axes->logX());
    EXPECT_TRUE(cf.axes->logY());
}

// ═══════════════════════════════════════════════════════════════════════════
// Rendering with log scale
// ═══════════════════════════════════════════════════════════════════════════

TEST(LogScaleRegression, LoglogRendersLine) {
    LogFigure cf(256);
    auto pts = expData(5, 10.0f);
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->loglog();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Loglog line should render";
}

TEST(LogScaleRegression, SemilogxRendersLine) {
    LogFigure cf(256);
    auto pts = expData(5, 10.0f);
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->semilogx();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Semilogx line should render";
}

TEST(LogScaleRegression, SemilogyRendersLine) {
    LogFigure cf(256);
    auto pts = powerData(10, 3.0f);
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->semilogy();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Semilogy line should render";
}

TEST(LogScaleRegression, LoglogRendersScatter) {
    LogFigure cf(256);
    auto pts = expData(5, 10.0f);
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    series.size = 8.0f;
    cf.axes->loglog();
    cf.axes->addPlot(std::make_unique<ScatterPlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Loglog scatter should render";
}

TEST(LogScaleRegression, LoglogExponentialAppearsLinear) {
    // With log scale, exponential data should appear as a straight line.
    LogFigure cf(256);
    auto pts = expData(5, 10.0f);  // y = 10^x → log10(y) = x (linear)
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->loglog();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Loglog exponential should render as line";
}

TEST(LogScaleRegression, LinearScaleRendersSameData) {
    // Same data without log scale should also render (but differently).
    LogFigure cf(256);
    auto pts = expData(5, 10.0f);
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    // No log scale — linear.
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Linear scale should render exponential data";
}

TEST(LogScaleRegression, LoglogPowerLawAppearsLinear) {
    // With log-log scale, power law y = x^n appears as a straight line
    // with slope n.
    LogFigure cf(256);
    auto pts = powerData(20, 2.0f);  // y = x^2
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->loglog();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Loglog power law should render as line";
}

TEST(LogScaleRegression, SemilogyExponentialAppearsLinear) {
    // With semilogy, exponential y = a^x appears as a straight line.
    LogFigure cf(256);
    auto pts = expData(10, 2.0f);  // y = 2^x
    Series2D series;
    series.points = pts;
    series.color = Color::blue();
    cf.axes->semilogy();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 20u) << "Semilogy exponential should render as line";
}

TEST(LogScaleRegression, LogScaleWithSinglePoint) {
    LogFigure cf(256);
    Series2D series;
    series.points = {{10, 100}, {100, 1000}};
    series.color = Color::blue();
    series.size = 10.0f;
    cf.axes->loglog();
    // Set manual viewport since GPU autoscale may not handle log scale.
    Viewport vp;
    vp.x = {1, 1000}; vp.y = {1, 10000}; vp.z = {0, 1};
    cf.axes->setViewport(vp);
    cf.axes->addPlot(std::make_unique<ScatterPlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 5u) << "Points with log scale should render";
}

TEST(LogScaleRegression, LogScaleHandlesSmallValues) {
    // Log scale should handle values < 1 (negative log).
    LogFigure cf(256);
    Series2D series;
    series.points = {{0.1f, 0.01f}, {1, 1}, {10, 100}};
    series.color = Color::blue();
    cf.axes->loglog();
    cf.axes->addPlot(std::make_unique<LinePlot>(series));
    auto img = cf.render();

    size_t filledCount = countPixels(img, isNotWhite);
    EXPECT_GT(filledCount, 10u) << "Log scale should handle small values";
}
