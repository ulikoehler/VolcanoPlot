// tests/test_parity11.cpp — tests for the 10-feature parity batch:
//   1. Line2D artist properties (Series2D marker colors, alpha)
//   2. Axes.set()/setp()/getp() (Python-side; C++ coverage via getters)
//   3. Axes getter batch (Python-side)
//   4. Axes introspection containers (Python-side)
//   5. relim/autoscale_view/autoscale + sticky edges + manual flags
//   6. Spine positions/bounds/style (raster + vector)
//   7. quiverkey reference arrow
//   8. xkcd/sketch parameters
//   9. clip_on for plot artists
//  10. stairs/pcolor/inset bindings (Python-side; StairsPlot here)
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/QuiverKeyPlot.hpp>
#include <volcano/plot/plots/StepPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

constexpr test::Pixel Red{255, 0, 0, 255};
constexpr test::Pixel Green{0, 255, 0, 255};
constexpr test::Pixel Black{0, 0, 0, 255};

struct BatchFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    // `fullBleed` zeroes the grid margins (full-canvas axes); otherwise
    // the default mpl margins leave room for tick/spine assertions.
    explicit BatchFigure(uint32_t size = 256, bool fullBleed = true)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        if (fullBleed) {
            figure.grid().left = 0.0f; figure.grid().right = 1.0f;
            figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
        }
    }

    Image render() { return harness.render(figure); }
};

size_t countNonWhite(const Image& img) {
    return img.countIf([](uint32_t, uint32_t, Pixel p) {
        return !(p.r > 230 && p.g > 230 && p.b > 230);
    });
}

std::unique_ptr<LinePlot> lineOf(std::vector<Point2D> pts, Color c) {
    Series2D s;
    s.points = std::move(pts);
    s.color = c;
    return std::make_unique<LinePlot>(std::move(s));
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Feature 1: Line2D artist property plumbing (Series2D)
// ═══════════════════════════════════════════════════════════════════════════

TEST(Line2DProps, AlphaMultipliesResolvedColor) {
    Series2D s;
    s.color = Color::red();
    s.alpha = 0.5f;
    auto c = s.resolvedColor();
    EXPECT_FLOAT_EQ(c.a, 0.5f);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
}

TEST(Line2DProps, AlphaAppliesToMarkerColors) {
    Series2D s;
    s.alpha = 0.25f;
    s.markerFaceColor = Color::blue();
    auto fc = s.applyAlpha(s.markerFaceColor);
    ASSERT_TRUE(fc.has_value());
    EXPECT_FLOAT_EQ(fc->a, 0.25f);
}

TEST(Line2DProps, MarkerFaceColorOverridesSeriesColor) {
    // A red line with green marker faces: marker pixels must be green.
    BatchFigure f;
    Series2D s;
    s.points = {{0.5f, 0.5f}};
    s.color = Color::red();
    s.marker = MarkerStyle::Circle;
    s.size = 30.0f;
    s.markerFaceColor = Color::green();
    s.lineStyle = LineStyle::None;
    f.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto img = f.render();
    EXPECT_GT(img.countColor(Green, 60), 50u);
    EXPECT_EQ(img.countColor(Red, 60), 0u);
}

TEST(Line2DProps, ClipOnFlagDefaultsTrue) {
    LinePlot p{Series2D{}};
    EXPECT_TRUE(p.clipOn);
    EXPECT_TRUE(p.visible);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 5: relim / autoscale_view / autoscale state
// ═══════════════════════════════════════════════════════════════════════════

TEST(AutoscaleApi, RelimComputesRawDataLim) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{2.0f, 5.0f}, {8.0f, 9.0f}}, Color::red()));
    ax->relim();
    const auto& d = ax->dataLim();
    EXPECT_FLOAT_EQ(d.x.min, 2.0f);
    EXPECT_FLOAT_EQ(d.x.max, 8.0f);
    EXPECT_FLOAT_EQ(d.y.min, 5.0f);
    EXPECT_FLOAT_EQ(d.y.max, 9.0f);
}

TEST(AutoscaleApi, SetXlimDisablesAutoscalex) {
    // mpl: set_xlim turns autoscalex_on off.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{0.0f, 0.0f}, {1.0f, 1.0f}}, Color::red()));
    ax->autoscale();
    EXPECT_TRUE(ax->getAutoscalexOn());
    ax->setXlim(0.0f, 10.0f);
    EXPECT_FALSE(ax->getAutoscalexOn());
    // ...but only for x.
    EXPECT_TRUE(ax->getAutoscaleyOn());
}

TEST(AutoscaleApi, AutoscaleReenablesFlag) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{0.0f, 0.0f}, {4.0f, 4.0f}}, Color::red()));
    ax->setXlim(-5.0f, 50.0f);
    ASSERT_FALSE(ax->getAutoscalexOn());
    ax->autoscale(true, "x");
    EXPECT_TRUE(ax->getAutoscalexOn());
    // View updated from data (with mpl 5% margin → ~[-0.2, 4.2]).
    auto v = ax->viewport();
    EXPECT_NEAR(v.x.min, -0.2f, 0.01f);
    EXPECT_NEAR(v.x.max, 4.2f, 0.01f);
}

TEST(AutoscaleApi, AutoscaleViewLeavesManualAxisAlone) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{0.0f, 0.0f}, {4.0f, 4.0f}}, Color::red()));
    ax->setXlim(-5.0f, 50.0f);
    ax->relim();
    ax->autoscaleView();
    auto v = ax->viewport();
    // Manual x untouched; y autoscaled with margin.
    EXPECT_FLOAT_EQ(v.x.min, -5.0f);
    EXPECT_FLOAT_EQ(v.x.max, 50.0f);
    EXPECT_NEAR(v.y.min, -0.2f, 0.01f);
}

TEST(AutoscaleApi, AutoscaleNoneLeavesFlagsUnchanged) {
    // mpl autoscale(enable=None): flags unchanged, view refreshed only
    // where autoscaling is on.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{0.0f, 0.0f}, {4.0f, 4.0f}}, Color::red()));
    ax->setXlim(-5.0f, 50.0f);
    ax->autoscale(std::nullopt);
    EXPECT_FALSE(ax->getAutoscalexOn());
    EXPECT_TRUE(ax->getAutoscaleyOn());
    EXPECT_FLOAT_EQ(ax->viewport().x.min, -5.0f);
}

TEST(AutoscaleApi, RelimVisibleOnlySkipsHiddenPlots) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(lineOf({{0.0f, 0.0f}, {1.0f, 1.0f}}, Color::red()));
    auto* hidden = ax->addPlot(
        lineOf({{-100.0f, -100.0f}, {100.0f, 100.0f}}, Color::blue()));
    ax->relim();
    EXPECT_LE(ax->dataLim().x.min, -99.0f);
    hidden->visible = false;
    ax->relim(/*visibleOnly=*/true);
    EXPECT_FLOAT_EQ(ax->dataLim().x.min, 0.0f);
    EXPECT_FLOAT_EQ(ax->dataLim().x.max, 1.0f);
}

TEST(AutoscaleApi, BarBaselineIsStickyEdge) {
    // mpl: bar() registers y=0 as a sticky edge — the autoscale margin
    // may not push the bottom below 0.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->addPlot(std::make_unique<BarPlot>(
        BarData{.heights = {1.0f, 2.0f, 3.0f}}));
    ax->autoscale();
    EXPECT_FLOAT_EQ(ax->viewport().y.min, 0.0f);
    EXPECT_GT(ax->viewport().y.max, 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 6: spine position / bounds / style
// ═══════════════════════════════════════════════════════════════════════════

TEST(SpinePosition, SpecDefaultsUnpositioned) {
    Axes::SpineSet ss;
    EXPECT_TRUE(ss.bottom.visible);
    EXPECT_FALSE(ss.bottom.positionSet);
    EXPECT_FALSE(ss.bottom.bounds.has_value());
}

TEST(SpinePosition, SideThrowsOnBadName) {
    Axes::SpineSet ss;
    EXPECT_THROW(ss.side("diagonal"), std::invalid_argument);
}

TEST(SpinePosition, DataPositionResolves) {
    // Bottom spine at data y=0.5 with a [0,1] viewport → the middle
    // pixel row of a 100px rect at y=0.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->setViewport({{0, 1}, {0, 1}, {0, 1}});
    auto& s = ax->spine("bottom");
    s.posMode = Axes::SpineSpec::PosMode::Data;
    s.posAmount = 0.5f;
    s.positionSet = true;
    auto g = ax->spineLine("bottom", Rect2D{0, 0, 100, 100});
    EXPECT_FLOAT_EQ(g.pos, 50.0f);   // mid-rect (Y-down pixels)
    EXPECT_FLOAT_EQ(g.from, 0.0f);
    EXPECT_FLOAT_EQ(g.to, 100.0f);
}

TEST(SpinePosition, AxesFractionResolves) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    auto& s = ax->spine("left");
    s.posMode = Axes::SpineSpec::PosMode::Axes;
    s.posAmount = 0.25f;
    s.positionSet = true;
    auto g = ax->spineLine("left", Rect2D{0, 0, 100, 100});
    EXPECT_FLOAT_EQ(g.pos, 25.0f);
}

TEST(SpinePosition, OutwardOffsetsBeyondEdge) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    auto& s = ax->spine("bottom");
    s.posMode = Axes::SpineSpec::PosMode::Outward;
    s.posAmount = 10.0f;
    s.positionSet = true;
    auto g = ax->spineLine("bottom", Rect2D{0, 0, 100, 100});
    EXPECT_FLOAT_EQ(g.pos, 110.0f);  // 10px below the bottom edge
}

TEST(SpinePosition, BoundsRestrictSpan) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->setViewport({{0, 1}, {0, 1}, {0, 1}});
    auto& s = ax->spine("bottom");
    s.bounds = std::pair{0.25f, 0.75f};
    auto g = ax->spineLine("bottom", Rect2D{0, 0, 100, 100});
    EXPECT_FLOAT_EQ(g.from, 25.0f);
    EXPECT_FLOAT_EQ(g.to, 75.0f);
}

TEST(SpinePosition, MovedSpineRendersMidAxes) {
    // mpl center-spine: bottom spine at data y=0 draws a horizontal
    // black line across the middle of the axes.
    BatchFigure f;
    f.axes->style().xAxis.visible = true;
    f.axes->style().xAxis.color = Color::black();
    f.axes->style().yAxis.visible = true;
    f.axes->setViewport({{-1, 1}, {-1, 1}, {0, 1}});
    auto& s = f.axes->spine("bottom");
    s.posMode = Axes::SpineSpec::PosMode::Data;
    s.posAmount = 0.0f;
    s.positionSet = true;
    f.axes->setSpineVisible("top", false);
    auto img = f.render();
    // Mid-canvas row should carry the spine; the bottom edge should not.
    auto mid = img.countColorInRegion(Black, 10, 126, 246, 130, 60);
    auto bot = img.countColorInRegion(Black, 10, 250, 246, 255, 60);
    EXPECT_GT(mid, 100u);
    EXPECT_EQ(bot, 0u);
}

TEST(SpinePosition, SpineColorOverridesAxisColor) {
    BatchFigure f;
    f.axes->style().xAxis.visible = true;
    f.axes->style().yAxis.visible = true;
    auto& s = f.axes->spine("bottom");
    s.color = Color::red();
    s.lineWidth = 4.0f;   // thicker so the quad clears the edge row
    auto img = f.render();
    auto red = img.countColorInRegion(Red, 10, 250, 246, 256, 60);
    EXPECT_GT(red, 50u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 7: quiverkey
// ═══════════════════════════════════════════════════════════════════════════

TEST(QuiverKey, ReferenceArrowRenders) {
    BatchFigure f;
    auto* q = static_cast<QuiverPlot*>(f.axes->addPlot(
        std::make_unique<QuiverPlot>(std::vector<float>{0.5f},
                                     std::vector<float>{0.5f},
                                     std::vector<float>{1.0f},
                                     std::vector<float>{0.0f},
                                     QuiverConfig{})));
    auto& key = f.axes->quiverKey(*q, 0.85f, 0.85f, 1.0f, "1 unit", "E");
    (void)key;
    auto img = f.render();
    // The reference arrow + label land near axes fraction (0.85, 0.85)
    // — upper-right region of a full-bleed canvas (Y-down pixels).
    auto dark = img.countColorInRegion(Black, 150, 0, 255, 80, 60);
    EXPECT_GT(dark, 5u);
}

TEST(QuiverKey, ParticipatesInPlotList) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    auto* q = static_cast<QuiverPlot*>(ax->addPlot(
        std::make_unique<QuiverPlot>(std::vector<float>{0.0f},
                                     std::vector<float>{0.0f},
                                     std::vector<float>{1.0f},
                                     std::vector<float>{0.0f},
                                     QuiverConfig{})));
    size_t before = ax->plots().size();
    ax->quiverKey(*q, 0.9f, 0.9f, 1.0f, "k", "N");
    EXPECT_EQ(ax->plots().size(), before + 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 8: xkcd / sketch params
// ═══════════════════════════════════════════════════════════════════════════

TEST(SketchParams, SetSketchParamsStoresValues) {
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->setSketchParams(2.0f, 64.0f, 8.0f);
    EXPECT_FLOAT_EQ(ax->style().sketchScale, 2.0f);
    EXPECT_FLOAT_EQ(ax->style().sketchLength, 64.0f);
    EXPECT_FLOAT_EQ(ax->style().sketchRandomness, 8.0f);
}

TEST(SketchParams, NoneScaleDisablesSketch) {
    // mpl set_sketch_params(scale=None): _sketch = None.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->setSketchParams(1.0f);
    ax->setSketchParams(std::nullopt);
    EXPECT_FLOAT_EQ(ax->style().sketchScale, 0.0f);
}

TEST(SketchParams, MplDefaultsForLengthAndRandomness) {
    // mpl: set_sketch_params(scale=s) uses length=128, randomness=16
    // when not given.
    Figure fig{1, 1};
    Axes* ax = fig.addAxes(0, 0);
    ax->setSketchParams(1.5f);
    EXPECT_FLOAT_EQ(ax->style().sketchLength, 128.0f);
    EXPECT_FLOAT_EQ(ax->style().sketchRandomness, 16.0f);
}

TEST(SketchParams, XkcdStyleEnablesWobble) {
    auto s = styles::xkcdStyle();
    EXPECT_GT(s.sketchScale, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 9: clip_on
// ═══════════════════════════════════════════════════════════════════════════

TEST(ClipOn, ClippedLineStaysInsideAxes) {
    // Diagonal running far past the top of the viewport: with
    // clip_on=true nothing may render in the top margin strip.
    BatchFigure f{256, /*fullBleed=*/false};
    f.axes->setViewport({{0, 1}, {0, 1}, {0, 1}});
    auto* p = static_cast<LinePlot*>(f.axes->addPlot(
        lineOf({{0.0f, 0.0f}, {1.0f, 100.0f}}, Color::red())));
    p->clipOn = true;
    auto img = f.render();
    auto& r = f.axes->rect;
    // Region strictly above the axes rect (top margin).
    auto outside = img.countColorInRegion(
        Red, 0, 0, 255, uint32_t(r.y) > 4 ? uint32_t(r.y) - 4 : 1, 40);
    EXPECT_EQ(outside, 0u);
}

TEST(ClipOn, UnclippedLineDrawsIntoMargin) {
    // Same line with clip_on=false must paint pixels above the axes
    // rect (mpl clip_on=False draws across the whole figure).
    BatchFigure f{256, /*fullBleed=*/false};
    f.axes->setViewport({{0, 1}, {0, 1}, {0, 1}});
    auto* p = static_cast<LinePlot*>(f.axes->addPlot(
        lineOf({{0.0f, 0.0f}, {1.0f, 100.0f}}, Color::red())));
    p->clipOn = false;
    auto img = f.render();
    auto& r = f.axes->rect;
    auto outside = img.countColorInRegion(
        Red, 0, 0, 255, uint32_t(r.y) > 4 ? uint32_t(r.y) - 4 : 1, 40);
    EXPECT_GT(outside, 0u);
}

TEST(ClipOn, InvisiblePlotRendersNothing) {
    BatchFigure f;
    auto* p = static_cast<LinePlot*>(f.axes->addPlot(
        lineOf({{0.0f, 0.0f}, {1.0f, 1.0f}}, Color::red())));
    p->visible = false;
    auto img = f.render();
    EXPECT_EQ(img.countColor(Red, 60), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 10: stairs
// ═══════════════════════════════════════════════════════════════════════════

TEST(StairsPlot, OutlineRenders) {
    BatchFigure f;
    f.axes->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{1.0f, 2.0f, 1.0f},
        std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f},
        Color::black(), 2.0f, false));
    auto img = f.render();
    EXPECT_GT(countNonWhite(img), 100u);
}

TEST(StairsPlot, FillRendersInterior) {
    BatchFigure f;
    f.axes->addPlot(std::make_unique<StairsPlot>(
        std::vector<float>{0.8f, 0.8f, 0.8f},
        std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f},
        Color::blue(), 2.0f, true));
    auto img = f.render();
    // Filled staircase occupies a large interior area.
    EXPECT_GT(countNonWhite(img), 5000u);
}
