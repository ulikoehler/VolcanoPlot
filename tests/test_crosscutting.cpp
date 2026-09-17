// tests/test_crosscutting.cpp — §16: zorder, picking, spines, colorbar extend,
// clabel, hist/boxplot/violin/errorbar variants
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Normalize.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/BoxPlot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/ErrorbarPlot.hpp>
#include <volcano/plot/plots/HistPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/ViolinPlot.hpp>

#include <cmath>

using namespace volcano;
using namespace volcano::plot;

namespace {

constexpr test::Pixel Red{255, 0, 0, 255};
constexpr test::Pixel Blue{0, 0, 255, 255};
constexpr test::Pixel White{255, 255, 255, 255};
constexpr test::Pixel Black{0, 0, 0, 255};

struct Fx {
    test::PlotTestHarness h{128, 128};
    Figure fig;
    Axes* ax = fig.addAxes();
    Fx() {
        ax->setStyle(test::flatTestStyle());
        fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
        fig.layout(Extent2D{128, 128});
        ax->setViewport({{0, 1}, {0, 1}});
    }
    test::Image render() { return h.render(fig); }
};

Series2D makeSeries(std::vector<Point2D> pts) {
    Series2D s;
    s.points = std::move(pts);
    s.usePropCycle = false;
    return s;
}

} // namespace

// ═══ zorder ═══════════════════════════════════════════════════════════════

TEST(ZOrder, HigherDrawsOnTop) {
    Fx fx;
    // Two overlapping bars: first blue, then red with higher zorder.
    BarData bd1; bd1.heights = {0.8f}; bd1.colors = {Color::blue()};
    BarData bd2; bd2.heights = {0.8f}; bd2.colors = {Color::red()};
    auto* b1 = static_cast<BarPlot*>(fx.ax->addPlot(std::make_unique<BarPlot>(bd1)));
    auto* b2 = static_cast<BarPlot*>(fx.ax->addPlot(std::make_unique<BarPlot>(bd2)));
    b2->zorder = 10.0f;
    auto img = fx.render();
    EXPECT_PIXEL_AT(img, 64, 40, Red, 40);
    EXPECT_PIXEL_COUNT(img, Blue, 0, 40);
}

TEST(ZOrder, LowerDrawsUnder) {
    Fx fx;
    BarData bd1; bd1.heights = {0.8f}; bd1.colors = {Color::blue()};
    BarData bd2; bd2.heights = {0.8f}; bd2.colors = {Color::red()};
    auto* b1 = static_cast<BarPlot*>(fx.ax->addPlot(std::make_unique<BarPlot>(bd1)));
    fx.ax->addPlot(std::make_unique<BarPlot>(bd2));
    b1->zorder = 10.0f;  // b1 (blue) drawn last despite earlier insertion
    auto img = fx.render();
    EXPECT_PIXEL_AT(img, 64, 40, Blue, 40);
}

TEST(ZOrder, RasterizedFlagStored) {
    Fx fx;
    auto* s = static_cast<ScatterPlot*>(fx.ax->addPlot(
        std::make_unique<ScatterPlot>(makeSeries({{0.5f, 0.5f}}))));
    s->rasterized = true;
    EXPECT_TRUE(s->rasterized);
    EXPECT_FLOAT_EQ(s->zorder, 0.0f);
}

// ═══ picking ══════════════════════════════════════════════════════════════

TEST(Picking, ScatterHit) {
    Fx fx;
    fx.ax->addPlot(std::make_unique<ScatterPlot>(
        makeSeries({{0.5f, 0.5f}})));
    auto hits = fx.ax->pick({0.5f, 0.5f});
    EXPECT_EQ(hits.size(), 1u);
    EXPECT_TRUE(fx.ax->pick({0.1f, 0.1f}).empty());
}

TEST(Picking, LineHit) {
    Fx fx;
    auto s = makeSeries({{0.0f, 0.5f}, {1.0f, 0.5f}});
    auto* lp = static_cast<LinePlot*>(fx.ax->addPlot(
        std::make_unique<LinePlot>(s)));
    lp->series().lineWidth = 8.0f;
    EXPECT_TRUE(fx.ax->pick({0.5f, 0.5f}).size() == 1u);
    EXPECT_TRUE(fx.ax->pick({0.5f, 0.9f}).empty());
}

TEST(Picking, BarHit) {
    Fx fx;
    BarData bd; bd.heights = {0.5f};
    fx.ax->addPlot(std::make_unique<BarPlot>(bd));
    EXPECT_EQ(fx.ax->pick({0.5f, 0.3f}).size(), 1u);
    EXPECT_TRUE(fx.ax->pick({0.5f, 0.8f}).empty());
}

TEST(Picking, PatchHit) {
    Fx fx;
    fx.ax->addPatch(patch::Rectangle(0.2f, 0.2f, 0.4f, 0.4f));
    EXPECT_EQ(fx.ax->pick({0.4f, 0.4f}).size(), 1u);
    EXPECT_TRUE(fx.ax->pick({0.9f, 0.9f}).empty());
}

TEST(Picking, TopmostFirst) {
    Fx fx;
    auto* a = static_cast<ScatterPlot*>(fx.ax->addPlot(
        std::make_unique<ScatterPlot>(makeSeries({{0.5f, 0.5f}}))));
    auto* b = static_cast<ScatterPlot*>(fx.ax->addPlot(
        std::make_unique<ScatterPlot>(makeSeries({{0.5f, 0.5f}}))));
    b->zorder = 5.0f;
    auto hits = fx.ax->pick({0.5f, 0.5f});
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], static_cast<const IPlot*>(b));  // topmost first
    EXPECT_EQ(hits[1], static_cast<const IPlot*>(a));
}

// ═══ spine visibility ═════════════════════════════════════════════════════

TEST(Spines, HideIndividualSpine) {
    Fx fx;
    // Black spines, then hide the top one.
    fx.ax->style().xAxis.visible = true;
    fx.ax->style().xAxis.color = Color::black();
    fx.ax->style().yAxis.visible = true;
    fx.ax->setSpineVisible("top", false);
    auto img = fx.render();
    // Top edge row should be background; bottom row should have the spine.
    auto topDark = img.countColorInRegion(Black, 10, 0, 118, 2, 60);
    auto botDark = img.countColorInRegion(Black, 10, 125, 118, 128, 60);
    EXPECT_EQ(topDark, 0u);
    EXPECT_GT(botDark, 50u);
}

TEST(Spines, AllVisibleByDefault) {
    Fx fx;
    fx.ax->style().xAxis.visible = true;
    fx.ax->style().xAxis.color = Color::black();
    fx.ax->style().yAxis.visible = true;
    auto img = fx.render();
    auto topDark = img.countColorInRegion(Black, 10, 0, 118, 2, 60);
    EXPECT_GT(topDark, 50u);
}

TEST(Spines, HideAll) {
    Fx fx;
    fx.ax->style().xAxis.visible = true;
    fx.ax->style().xAxis.color = Color::black();
    fx.ax->style().yAxis.visible = true;
    fx.ax->setSpineVisible("all", false);
    auto img = fx.render();
    EXPECT_EQ(img.countColor(Black, 60), 0u);
}

// ═══ colorbar extend + norm ═══════════════════════════════════════════════

TEST(ColorbarExtend, MinMaxTrianglesDraw) {
    test::PlotTestHarness h2{256, 128};
    Figure fig2;
    Axes* ax2 = fig2.addAxes();
    ax2->setStyle(test::flatTestStyle());
    // Axes occupies the left half → colorbar strip lands inside the canvas.
    fig2.subplotsAdjust(0, 0, 0.5f, 1, 0, 0);
    fig2.layout(Extent2D{256, 128});
    ax2->setViewport({{0, 1}, {0, 1}, {0, 1}});
    ax2->style().colorbar.visible = true;
    ax2->style().colorbar.extend = "both";
    ax2->style().colorbar.width = 16.0f;
    ax2->style().colorbar.labelColor = Color{0, 0, 0, 0};
    auto img = h2.render(fig2);
    // Colored strip pixels right of x=128.
    auto nonWhite = img.countIf([](uint32_t x, uint32_t, test::Pixel p) {
        return x >= 132 && x < 150 &&
               (p.r < 240 || p.g < 240 || p.b < 240);
    });
    EXPECT_GT(nonWhite, 200u);
    // Extend triangles: colored pixels at the strip's center column above/
    // below the body (triangle apexes).
    auto topTri = img.countIf([](uint32_t x, uint32_t y, test::Pixel p) {
        return x >= 136 && x <= 144 && y <= 6 &&
               (p.r < 240 || p.g < 240 || p.b < 240);
    });
    auto botTri = img.countIf([](uint32_t x, uint32_t y, test::Pixel p) {
        return x >= 136 && x <= 144 && y >= 122 &&
               (p.r < 240 || p.g < 240 || p.b < 240);
    });
    EXPECT_GT(topTri, 2u);
    EXPECT_GT(botTri, 2u);
}

TEST(ColorbarExtend, NormMapsColors) {
    test::PlotTestHarness h2{256, 128};
    Figure fig2;
    Axes* ax2 = fig2.addAxes();
    ax2->setStyle(test::flatTestStyle());
    fig2.subplotsAdjust(0, 0, 0.5f, 1, 0, 0);
    fig2.layout(Extent2D{256, 128});
    ax2->setViewport({{0, 1}, {0, 1}, {1, 100}});
    ax2->style().colorbar.visible = true;
    ax2->style().colorbar.colormap = "viridis";
    ax2->style().colorbar.norm = std::make_shared<LogNorm>(1.0f, 100.0f);
    auto img = h2.render(fig2);
    auto nonWhite = img.countIf([](uint32_t x, uint32_t, test::Pixel p) {
        return x >= 132 && x < 150 &&
               (p.r < 240 || p.g < 240 || p.b < 240);
    });
    EXPECT_GT(nonWhite, 100u);
}

// ═══ clabel ═══════════════════════════════════════════════════════════════

TEST(Clabel, ContourLabelsRender) {
    Fx fx;
    // Radial bump field → closed contours.
    Grid2D g;
    g.width = g.height = 24;
    g.xRange = {0, 1};
    g.yRange = {0, 1};
    g.values.resize(24 * 24);
    for (uint32_t j = 0; j < 24; ++j)
        for (uint32_t i = 0; i < 24; ++i) {
            float dx = i / 23.0f - 0.5f, dy = j / 23.0f - 0.5f;
            g.values[j * 24 + i] = 1.0f - (dx * dx + dy * dy) * 4.0f;
        }
    ContourConfig cfg;
    cfg.levels = {0.2f, 0.5f, 0.8f};
    cfg.clabel = true;
    cfg.lineColor = Color::black();
    fx.ax->addPlot(std::make_unique<ContourPlot>(g, cfg));
    auto img = fx.render();
    // Contour lines render.
    EXPECT_PIXEL_COUNT(img, Black, 100, 60);
}

// ═══ hist types ═══════════════════════════════════════════════════════════

TEST(HistType, BarSingleDataset) {
    Fx fx;
    HistConfig cfg;
    cfg.binEdges = {0.0f, 0.5f, 1.0f};
    cfg.bins = HistBinMethod::Edges;
    cfg.color = Color::red();
    cfg.color.a = 1.0f;
    fx.ax->addPlot(std::make_unique<HistPlot>(
        std::vector<float>{0.1f, 0.2f, 0.3f, 0.7f}, cfg));
    fx.ax->setViewport({{0, 1}, {0, 4}});
    auto img = fx.render();
    // Bin 0 (3 samples) is taller than bin 1 (1 sample).
    EXPECT_PIXEL_AT(img, 32, 100, Red, 40);   // bin0 lower region filled
    EXPECT_PIXEL_AT(img, 96, 40, White, 60);  // bin1 top empty
}

TEST(HistType, StepOutlineOnly) {
    Fx fx;
    HistConfig cfg;
    cfg.binEdges = {0.0f, 0.5f, 1.0f};
    cfg.bins = HistBinMethod::Edges;
    cfg.histtype = HistType::Step;
    cfg.color = Color::blue();
    cfg.stepLineWidth = 3.0f;
    fx.ax->addPlot(std::make_unique<HistPlot>(
        std::vector<float>{0.1f, 0.2f, 0.3f, 0.7f}, cfg));
    fx.ax->setViewport({{0, 1}, {0, 4}});
    auto img = fx.render();
    // Outline pixels exist but the bin interior is NOT filled.
    EXPECT_PIXEL_COUNT(img, Blue, 40, 60);
    EXPECT_PIXEL_AT(img, 32, 80, White, 60);  // interior of bin 0
}

TEST(HistType, StepFilledFillsToBaseline) {
    Fx fx;
    HistConfig cfg;
    cfg.binEdges = {0.0f, 0.5f, 1.0f};
    cfg.bins = HistBinMethod::Edges;
    cfg.histtype = HistType::StepFilled;
    cfg.color = Color::blue();
    cfg.color.a = 1.0f;
    fx.ax->addPlot(std::make_unique<HistPlot>(
        std::vector<float>{0.1f, 0.2f, 0.3f, 0.7f}, cfg));
    fx.ax->setViewport({{0, 1}, {0, 4}});
    auto img = fx.render();
    EXPECT_PIXEL_AT(img, 32, 80, Blue, 40);   // filled interior
    EXPECT_PIXEL_AT(img, 96, 30, White, 60);  // above bin1's top
}

TEST(HistType, BarStackedSumsHeights) {
    Fx fx;
    HistConfig cfg;
    cfg.binEdges = {0.0f, 1.0f};
    cfg.bins = HistBinMethod::Edges;
    cfg.histtype = HistType::BarStacked;
    cfg.colors = {Color::red(), Color::blue()};
    cfg.colors[0].a = 1.0f;
    cfg.colors[1].a = 1.0f;
    fx.ax->addPlot(std::make_unique<HistPlot>(
        std::vector<std::vector<float>>{{0.5f}, {0.5f}}, cfg));
    fx.ax->setViewport({{0, 1}, {0, 4}});
    auto img = fx.render();
    // Each dataset contributes height 1 → bottom half red, above it blue.
    EXPECT_PIXEL_AT(img, 64, 108, Red, 40);   // bottom of stack
    EXPECT_PIXEL_AT(img, 64, 76, Blue, 40);   // second layer
    EXPECT_PIXEL_AT(img, 64, 30, White, 60);  // above the stack
}

TEST(HistType, BarSideBySideMultiDataset) {
    Fx fx;
    HistConfig cfg;
    cfg.binEdges = {0.0f, 1.0f};
    cfg.bins = HistBinMethod::Edges;
    cfg.colors = {Color::red(), Color::blue()};
    cfg.colors[0].a = 1.0f;
    cfg.colors[1].a = 1.0f;
    fx.ax->addPlot(std::make_unique<HistPlot>(
        std::vector<std::vector<float>>{{0.5f}, {0.5f}}, cfg));
    fx.ax->setViewport({{0, 1}, {0, 4}});
    auto img = fx.render();
    // Two datasets share the bin side-by-side: left half red, right blue.
    EXPECT_PIXEL_AT(img, 40, 100, Red, 40);
    EXPECT_PIXEL_AT(img, 88, 100, Blue, 40);
}

// ═══ boxplot variants ═════════════════════════════════════════════════════

TEST(BoxPlotVariant, NotchStatsComputed) {
    Fx fx;
    BoxPlotConfig cfg;
    cfg.notch = true;
    auto bp = std::make_unique<BoxPlot>(
        std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, cfg);
    auto* raw = bp.get();
    fx.ax->addPlot(std::move(bp));
    fx.render();  // prepare() computes stats
    ASSERT_EQ(raw->stats().size(), 1u);
    const auto& s = raw->stats()[0];
    EXPECT_LT(s.notchLo, s.median);
    EXPECT_GT(s.notchHi, s.median);
    EXPECT_FLOAT_EQ(s.mean, 5.5f);
}

TEST(BoxPlotVariant, BootstrapCI) {
    Fx fx;
    BoxPlotConfig cfg;
    cfg.notch = true;
    cfg.bootstrap = 200;
    auto bp = std::make_unique<BoxPlot>(
        std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, cfg);
    auto* raw = bp.get();
    fx.ax->addPlot(std::move(bp));
    fx.render();
    const auto& s = raw->stats()[0];
    EXPECT_LT(s.notchLo, s.median);
    EXPECT_GT(s.notchHi, s.median);
    // Bootstrap CI within data range.
    EXPECT_GE(s.notchLo, s.min - 0.01f);
    EXPECT_LE(s.notchHi, s.max + 0.01f);
}

TEST(BoxPlotVariant, NotchedBoxRenders) {
    Fx fx;
    BoxPlotConfig cfg;
    cfg.notch = true;
    cfg.boxColor = Color::red();
    cfg.boxEdgeColor = Color::red();
    cfg.whiskerColor = Color::red();
    cfg.medianColor = Color::red();
    cfg.showOutliers = false;
    std::vector<float> data(40);
    for (int i = 0; i < 40; ++i) data[i] = 0.2f + 0.6f * i / 39.0f;
    fx.ax->addPlot(std::make_unique<BoxPlot>(data, cfg));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 100, 40);
}

TEST(BoxPlotVariant, ShowMeans) {
    Fx fx;
    BoxPlotConfig cfg;
    cfg.showMeans = true;
    cfg.meanColor = Color::blue();
    cfg.boxColor = Color{0,0,0,0};
    cfg.boxEdgeColor = Color::red();
    cfg.whiskerColor = Color::red();
    cfg.medianColor = Color::red();
    cfg.showOutliers = false;
    std::vector<float> data(20);
    for (int i = 0; i < 20; ++i) data[i] = 0.3f + 0.4f * i / 19.0f;
    fx.ax->addPlot(std::make_unique<BoxPlot>(data, cfg));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Blue, 5, 60);
}

// ═══ violin variants ══════════════════════════════════════════════════════

TEST(ViolinVariant, CustomPositionsAndWidths) {
    Fx fx;
    ViolinConfig cfg;
    cfg.positions = {0.3f, 0.8f};
    cfg.widths = {0.1f, 0.4f};
    cfg.showMean = false;
    cfg.showExtrema = false;
    std::vector<float> d1{0.3f, 0.5f, 0.7f, 0.5f, 0.4f};
    std::vector<float> d2{0.4f, 0.6f, 0.5f, 0.55f, 0.45f};
    fx.ax->addPlot(std::make_unique<ViolinPlot>(
        std::vector<std::vector<float>>{d1, d2}, cfg));
    fx.render();  // trigger autoscale
    auto vp = fx.ax->viewport();
    // Autoscale centers on positions 0.3 and 0.8.
    EXPECT_LT(vp.x.min, 0.3f);
    EXPECT_GT(vp.x.max, 0.8f);
}

TEST(ViolinVariant, HorizontalViolins) {
    Fx fx;
    ViolinConfig cfg;
    cfg.vert = false;
    cfg.showMean = false;
    cfg.showExtrema = false;
    cfg.bodyColor = Color::red();
    cfg.bodyColor.a = 1.0f;
    cfg.edgeColor = Color{0,0,0,0};
    std::vector<float> d{0.2f, 0.4f, 0.6f, 0.8f, 0.5f};
    fx.ax->addPlot(std::make_unique<ViolinPlot>(d, cfg));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 50, 40);
}

TEST(ViolinVariant, PerBodyColors) {
    Fx fx;
    ViolinConfig cfg;
    cfg.bodyColors = {Color::red(), Color::blue()};
    cfg.bodyColors[0].a = 1.0f;
    cfg.bodyColors[1].a = 1.0f;
    cfg.edgeColor = Color{0,0,0,0};
    cfg.showMean = false;
    cfg.showExtrema = false;
    cfg.positions = {0.3f, 0.7f};
    std::vector<float> d{0.4f, 0.5f, 0.6f, 0.5f};
    fx.ax->addPlot(std::make_unique<ViolinPlot>(
        std::vector<std::vector<float>>{d, d}, cfg));
    auto img = fx.render();
    EXPECT_PIXEL_COUNT(img, Red, 30, 40);
    EXPECT_PIXEL_COUNT(img, Blue, 30, 40);
}

// ═══ errorbar variants ════════════════════════════════════════════════════

TEST(ErrorbarVariant, ErrorEverySkipsPoints) {
    Fx fx;
    ErrorbarConfig cfg;
    cfg.drawLine = false;
    cfg.drawMarker = false;
    cfg.drawCaps = true;
    cfg.errorevery = 2;             // every 2nd point → 2 error bars
    cfg.yerr = {0.1f, 0.1f, 0.1f, 0.1f};
    cfg.errorbarColor = Color::red();
    cfg.errorbarWidth = 3.0f;
    fx.ax->addPlot(std::make_unique<ErrorbarPlot>(
        std::vector<float>{0.2f, 0.4f, 0.6f, 0.8f},
        std::vector<float>{0.5f, 0.5f, 0.5f, 0.5f}, cfg));
    auto img = fx.render();
    // Points 0 and 2 get bars (x≈0.2→26px, 0.6→77px); points 1,3 don't.
    EXPECT_GT(img.countColorInRegion(Red, 22, 40, 30, 88, 60), 0u);
    EXPECT_GT(img.countColorInRegion(Red, 73, 40, 81, 88, 60), 0u);
    EXPECT_EQ(img.countColorInRegion(Red, 47, 40, 55, 88, 60), 0u);
    EXPECT_EQ(img.countColorInRegion(Red, 98, 40, 106, 88, 60), 0u);
}

TEST(ErrorbarVariant, UplimsArrowhead) {
    Fx fx;
    ErrorbarConfig cfg;
    cfg.drawLine = false;
    cfg.drawMarker = false;
    cfg.drawCaps = false;
    cfg.yerr = {0.2f};
    cfg.uplims = {true};           // upper limit → bar down + arrow up
    cfg.errorbarColor = Color::red();
    cfg.errorbarWidth = 3.0f;
    cfg.capSize = 8.0f;
    fx.ax->addPlot(std::make_unique<ErrorbarPlot>(
        std::vector<float>{0.5f}, std::vector<float>{0.6f}, cfg));
    auto img = fx.render();
    // Arrowhead near (0.5, 0.6) → px (64, 51); bar below extends to y=0.4.
    EXPECT_PIXEL_COUNT(img, Red, 20, 60);
    // Nothing above the point (y data > 0.6 → pixel y < 51-8).
    auto above = img.countColorInRegion(Red, 40, 0, 88, 38, 60);
    EXPECT_EQ(above, 0u);
}
