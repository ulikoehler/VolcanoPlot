// tests/test_axes_figure.cpp — §4 Axes/Figure features:
// GridSpec, subplot_mosaic, subplot2grid, subfigures, shared/twin/secondary
// axes, inset/locatable axes, scales, projections, aspect, layout controls.
#include <gtest/gtest.h>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/GridSpec.hpp>
#include <volcano/plot/Scale.hpp>
#include <volcano/plot/Projection.hpp>
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <cmath>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

// ─── GridSpec geometry ─────────────────────────────────────────────────────

TEST(GridSpec, UniformCellsSplitRegionEvenly) {
    GridSpec g(2, 2);
    g.left = 0.0f; g.right = 1.0f; g.bottom = 0.0f; g.top = 1.0f;
    g.wspace = 0.0f; g.hspace = 0.0f;
    Rect2D fig{0, 0, 400, 400};
    auto tl = g.cellRect(g.region(fig), g.at(0, 0));
    auto br = g.cellRect(g.region(fig), g.at(1, 1));
    EXPECT_EQ(tl.width, 200u);
    EXPECT_EQ(tl.height, 200u);
    EXPECT_EQ(br.x, 200);
    EXPECT_EQ(br.y, 200);
}

TEST(GridSpec, SpansCoverMultipleCells) {
    GridSpec g(2, 3);
    g.left = 0.0f; g.right = 1.0f; g.bottom = 0.0f; g.top = 1.0f;
    g.wspace = 0.0f; g.hspace = 0.0f;
    Rect2D fig{0, 0, 600, 400};
    // Span all 3 columns of row 1.
    auto r = g.cellRect(g.region(fig), g.at(1, 0, 1, 3));
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.width, 600u);
    EXPECT_EQ(r.y, 200);
    EXPECT_EQ(r.height, 200u);
}

TEST(GridSpec, WidthRatiosChangeCellSizes) {
    GridSpec g(1, 2);
    g.left = 0.0f; g.right = 1.0f; g.bottom = 0.0f; g.top = 1.0f;
    g.wspace = 0.0f;
    g.widthRatios = {3.0f, 1.0f};
    Rect2D fig{0, 0, 400, 100};
    auto left = g.cellRect(g.region(fig), g.at(0, 0));
    auto right = g.cellRect(g.region(fig), g.at(0, 1));
    EXPECT_EQ(left.width, 300u);
    EXPECT_EQ(right.width, 100u);
    EXPECT_EQ(right.x, 300);
}

TEST(GridSpec, MarginsInsetTheRegion) {
    GridSpec g(1, 1);
    g.left = 0.1f; g.right = 0.9f; g.bottom = 0.2f; g.top = 0.8f;
    Rect2D fig{0, 0, 1000, 1000};
    auto r = g.region(fig);
    EXPECT_EQ(r.x, 100);
    EXPECT_EQ(r.width, 800u);
    // top=0.8 → y offset 0.2 from top (Y-down).
    EXPECT_EQ(r.y, 200);
    EXPECT_EQ(r.height, 600u);
}

TEST(GridSpec, NestedGridResolvesInsideParentCell) {
    GridSpec g(1, 2);
    g.left = 0.0f; g.right = 1.0f; g.bottom = 0.0f; g.top = 1.0f;
    g.wspace = 0.0f;
    auto sub = g.at(0, 1).nested(1, 2);
    sub->left = 0.0f; sub->right = 1.0f; sub->bottom = 0.0f; sub->top = 1.0f;
    sub->wspace = 0.0f;
    Rect2D fig{0, 0, 800, 400};
    // Parent cell = right half (x 400..800). Nested cell 0 = x 400..600.
    auto r = sub->cellRect(sub->resolveRegion(fig), sub->at(0, 0));
    EXPECT_EQ(r.x, 400);
    EXPECT_EQ(r.width, 200u);
}

// ─── subplot2grid / subplot_mosaic ─────────────────────────────────────────

TEST(Figure, Subplot2GridCreatesGridAndAxes) {
    Figure fig;
    auto* ax = fig.subplot2grid({2, 2}, {0, 0});
    ASSERT_NE(ax, nullptr);
    EXPECT_EQ(fig.grid().rows(), 2u);
    EXPECT_EQ(fig.grid().cols(), 2u);
    fig.layout(Extent2D{400, 400});
    EXPECT_GT(ax->rect.width, 0u);
}

TEST(Figure, SubplotMosaicCreatesLabeledAxes) {
    Figure fig;
    auto axes = fig.subplotMosaic({{"A", "B"},
                                   {"A", "."}});
    ASSERT_EQ(axes.size(), 2u);
    ASSERT_NE(axes["A"], nullptr);
    ASSERT_NE(axes["B"], nullptr);
    fig.layout(Extent2D{400, 400});
    // "A" spans both rows of column 0 → full height.
    EXPECT_GT(axes["A"]->rect.height, axes["B"]->rect.height);
}

TEST(Figure, SubplotMosaicEmptyLabelLeavesCellEmpty) {
    Figure fig;
    auto axes = fig.subplotMosaic({{"A", "."}});
    ASSERT_EQ(axes.size(), 1u);
}

// ─── Subfigures ────────────────────────────────────────────────────────────

TEST(Figure, SubfiguresSplitFigure) {
    Figure fig;
    auto subs = fig.subfigures(1, 2);
    ASSERT_EQ(subs.size(), 2u);
    auto* ax0 = subs[0]->addAxes(0, 0);
    auto* ax1 = subs[1]->addAxes(0, 0);
    fig.layout(Extent2D{800, 400});
    // Left subfigure axes should be in the left half.
    EXPECT_LT(ax0->rect.x + (int)ax0->rect.width, 450);
    // Right subfigure axes in the right half.
    EXPECT_GT(ax1->rect.x, 350);
}

TEST(Figure, NestedSubplotSpecInSubfigure) {
    Figure fig(1, 1);
    auto* sub = fig.addSubfigure(fig.grid().at(0, 0), 1, 2);
    auto* ax = sub->addAxes(0, 1);
    fig.layout(Extent2D{400, 400});
    EXPECT_GT(ax->rect.x, 0);  // second cell → right half of subfig
}

// ─── Shared axes ───────────────────────────────────────────────────────────

TEST(SharedAxes, ShareXPropagatesLimits) {
    Figure fig(2, 1);
    auto* a = fig.addAxes(0, 0);
    auto* b = fig.addAxes(1, 0);
    a->shareX(*b);
    a->setXlim(-5.0f, 7.0f);
    fig.layout(Extent2D{400, 400});
    EXPECT_NEAR(b->viewport().x.min, -5.0f, 1e-4f);
    EXPECT_NEAR(b->viewport().x.max, 7.0f, 1e-4f);
}

TEST(SharedAxes, ShareYPropagatesLimits) {
    Figure fig(1, 2);
    auto* a = fig.addAxes(0, 0);
    auto* b = fig.addAxes(0, 1);
    a->shareY(*b);
    a->setYlim(2.0f, 9.0f);
    fig.layout(Extent2D{400, 400});
    EXPECT_NEAR(b->viewport().y.min, 2.0f, 1e-4f);
    EXPECT_NEAR(b->viewport().y.max, 9.0f, 1e-4f);
}

// ─── Twin / secondary axes ─────────────────────────────────────────────────

TEST(TwinAxes, TwinxSharesXRange) {
    Figure fig;
    auto* a = fig.addAxes(0, 0);
    auto* b = fig.twinx(*a);
    ASSERT_NE(b, nullptr);
    a->setXlim(0.0f, 10.0f);
    fig.layout(Extent2D{400, 400});
    // Overlay: same rect as parent.
    EXPECT_EQ(b->rect.x, a->rect.x);
    EXPECT_EQ(b->rect.width, a->rect.width);
    // Shared x.
    EXPECT_NEAR(b->viewport().x.min, 0.0f, 1e-4f);
    EXPECT_NEAR(b->viewport().x.max, 10.0f, 1e-4f);
    EXPECT_TRUE(b->yTicksRight());
}

TEST(TwinAxes, TwinySharesYRange) {
    Figure fig;
    auto* a = fig.addAxes(0, 0);
    auto* b = fig.twiny(*a);
    ASSERT_NE(b, nullptr);
    a->setYlim(1.0f, 4.0f);
    fig.layout(Extent2D{400, 400});
    EXPECT_NEAR(b->viewport().y.min, 1.0f, 1e-4f);
    EXPECT_NEAR(b->viewport().y.max, 4.0f, 1e-4f);
    EXPECT_TRUE(b->xTicksTop());
}

TEST(SecondaryAxis, StoresFunctionsAndLabel) {
    Axes ax;
    ax.secondaryXaxis([](float x) { return x * 2.0f; },
                      [](float x) { return x * 0.5f; },
                      "double");
    ASSERT_TRUE(ax.secondaryX().has_value());
    EXPECT_TRUE(ax.secondaryX()->enabled);
    EXPECT_EQ(ax.secondaryX()->label, "double");
    EXPECT_NEAR(ax.secondaryX()->forward(3.0f), 6.0f, 1e-4f);
    EXPECT_NEAR(ax.secondaryX()->inverse(6.0f), 3.0f, 1e-4f);
}

TEST(SecondaryAxis, YAxisWorks) {
    Axes ax;
    ax.secondaryYaxis([](float y) { return y + 100.0f; },
                      [](float y) { return y - 100.0f; });
    ASSERT_TRUE(ax.secondaryY().has_value());
    EXPECT_NEAR(ax.secondaryY()->forward(5.0f), 105.0f, 1e-4f);
}

// ─── Inset / located axes ──────────────────────────────────────────────────

TEST(InsetAxes, InsetIsFractionOfParent) {
    Figure fig;
    auto* parent = fig.addAxes(0, 0);
    auto* inset = fig.insetAxes(*parent, 0.5f, 0.5f, 0.4f, 0.4f);
    ASSERT_NE(inset, nullptr);
    fig.layout(Extent2D{400, 400});
    // Inset width/height = 40% of parent.
    EXPECT_NEAR((float)inset->rect.width, parent->rect.width * 0.4f, 2.0f);
    EXPECT_NEAR((float)inset->rect.height, parent->rect.height * 0.4f, 2.0f);
    // Inside parent bounds.
    EXPECT_GE(inset->rect.x, parent->rect.x);
    EXPECT_GE(inset->rect.y, parent->rect.y);
}

TEST(LocatedAxes, AppendRightOfParent) {
    Figure fig;
    auto* parent = fig.addAxes(0, 0);
    auto* cb = fig.appendAxes(*parent, Side::Right, 0.05f, 0.02f);
    ASSERT_NE(cb, nullptr);
    fig.layout(Extent2D{400, 400});
    EXPECT_GT(cb->rect.x, parent->rect.x + (int)parent->rect.width);
    EXPECT_LT(cb->rect.width, parent->rect.width);  // narrow colorbar strip
}

// ─── Scales ────────────────────────────────────────────────────────────────

TEST(Scale, LogForwardInverse) {
    auto s = AxisScale::log();
    EXPECT_NEAR(s.forward(100.0f), 2.0f, 1e-4f);
    EXPECT_NEAR(s.inverse(2.0f), 100.0f, 1e-3f);
}

TEST(Scale, SymlogLinearNearZero) {
    auto s = AxisScale::symlog(2.0f, 1.0f);
    // Within ±linthresh: linear.
    EXPECT_NEAR(s.forward(1.0f), 1.0f, 1e-4f);
    // Beyond: log.
    EXPECT_GT(s.forward(100.0f), s.forward(2.0f));
    // Roundtrip.
    EXPECT_NEAR(s.inverse(s.forward(50.0f)), 50.0f, 0.1f);
}

TEST(Scale, LogitCenterAtZero) {
    auto s = AxisScale::logit();
    EXPECT_NEAR(s.forward(0.5f), 0.0f, 1e-4f);
    EXPECT_GT(s.forward(0.9f), 0.0f);
    EXPECT_LT(s.forward(0.1f), 0.0f);
    EXPECT_NEAR(s.inverse(s.forward(0.75f)), 0.75f, 1e-4f);
}

TEST(Scale, AsinhSymmetric) {
    auto s = AxisScale::asinh(1.0f);
    EXPECT_NEAR(s.forward(0.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(s.forward(1.0f), -s.forward(-1.0f), 1e-4f);
    EXPECT_NEAR(s.inverse(s.forward(10.0f)), 10.0f, 0.1f);
}

TEST(Scale, MercatorLatitude) {
    auto s = AxisScale::mercator();
    EXPECT_NEAR(s.forward(0.0f), 0.0f, 1e-4f);
    EXPECT_GT(s.forward(1.0f), s.forward(0.5f));
    EXPECT_NEAR(s.inverse(s.forward(0.5f)), 0.5f, 1e-3f);
}

TEST(Scale, FunctionScaleUsesProvidedFns) {
    auto s = AxisScale::function([](float v) { return v * v; },
                                 [](float v) { return std::sqrt(v); });
    EXPECT_NEAR(s.forward(3.0f), 9.0f, 1e-4f);
    EXPECT_NEAR(s.inverse(9.0f), 3.0f, 1e-4f);
    EXPECT_FALSE(s.shaderSupported());
}

TEST(Scale, SetXscaleByName) {
    Axes ax;
    ax.setXscale("log");
    EXPECT_EQ(ax.xscale().kind, ScaleKind::Log);
    EXPECT_TRUE(ax.logX());
    ax.setXscale("symlog");
    EXPECT_EQ(ax.xscale().kind, ScaleKind::Symlog);
    ax.setXscale("linear");
    EXPECT_EQ(ax.xscale().kind, ScaleKind::Linear);
    EXPECT_FALSE(ax.logX());
}

TEST(Scale, SetLogXBackwardCompat) {
    Axes ax;
    ax.setLogX(true);
    EXPECT_TRUE(ax.logX());
    EXPECT_EQ(ax.xscale().kind, ScaleKind::Log);
    ax.setLogX(false);
    EXPECT_EQ(ax.xscale().kind, ScaleKind::Linear);
}

TEST(Scale, TransformViewIsDisplaySpace) {
    Axes ax;
    ax.setViewport({1, 100, 0, 1, 0, 1});
    ax.setXscale("log");
    auto t = ax.transform();
    // view.x should be log10(1..100) = [0, 2].
    EXPECT_NEAR(t.view.x.min, 0.0f, 1e-4f);
    EXPECT_NEAR(t.view.x.max, 2.0f, 1e-4f);
    EXPECT_EQ(t.codeX(), ScaleKind::Log);
    EXPECT_EQ(t.codeY(), ScaleKind::Linear);
}

TEST(Scale, ScaleTicksLogDecades) {
    auto s = AxisScale::log();
    auto ticks = scaleTicks(s, 1.0f, 1000.0f);
    ASSERT_GE(ticks.size(), 3u);
    // Should contain powers of 10.
    bool has100 = false;
    for (float t : ticks) if (std::abs(t - 100.0f) < 1e-3f) has100 = true;
    EXPECT_TRUE(has100);
}

TEST(Scale, DataToFractionLogScale) {
    Axes ax;
    ax.setViewport({1, 100, 0, 1, 0, 1});
    ax.setXscale("log");
    // log10(10) = 1 → halfway between log(1)=0 and log(100)=2.
    auto f = ax.dataToFraction({10.0f, 0.0f});
    EXPECT_NEAR(f.x, 0.5f, 1e-3f);
}

// ─── Projections ───────────────────────────────────────────────────────────

TEST(Projection, ParseNames) {
    EXPECT_EQ(Projection::parse("rectilinear").kind, ProjectionKind::Rectilinear);
    EXPECT_EQ(Projection::parse("polar").kind, ProjectionKind::Polar);
    EXPECT_EQ(Projection::parse("aitoff").kind, ProjectionKind::Aitoff);
    EXPECT_EQ(Projection::parse("hammer").kind, ProjectionKind::Hammer);
    EXPECT_EQ(Projection::parse("lambert").kind, ProjectionKind::Lambert);
    EXPECT_EQ(Projection::parse("mollweide").kind, ProjectionKind::Mollweide);
}

TEST(Projection, PolarForward) {
    Projection p = Projection::parse("polar");
    // theta=0, r=1 → (1, 0).
    auto q = p.forward({0.0f, 1.0f});
    EXPECT_NEAR(q.x, 1.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    // theta=pi/2, r=1 → (0, 1).
    q = p.forward({1.5707963f, 1.0f});
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 1.0f, 1e-4f);
}

TEST(Projection, PolarThetaOffsetAndDirection) {
    Projection p = Projection::parse("polar");
    p.thetaOffset = 1.5707963f;  // theta=0 at north
    auto q = p.forward({0.0f, 1.0f});
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 1.0f, 1e-4f);
    // Clockwise direction.
    p.thetaOffset = 0.0f; p.thetaDir = -1.0f;
    q = p.forward({1.5707963f, 1.0f});
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, -1.0f, 1e-4f);
}

TEST(Projection, MollweideForward) {
    Projection p = Projection::parse("mollweide");
    auto q = p.forward({0.0f, 0.0f});
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    // Equator at lon=pi: x = 2√2/pi * pi * cos(0) = 2√2.
    q = p.forward({3.14159265f, 0.0f});
    EXPECT_NEAR(q.x, 2.0f * 1.41421356f, 1e-3f);
    EXPECT_NEAR(q.y, 0.0f, 1e-3f);
}

TEST(Projection, AitoffForward) {
    Projection p = Projection::parse("aitoff");
    auto q = p.forward({0.0f, 0.0f});
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
}

TEST(Projection, AxesSetProjectionByName) {
    Axes ax;
    ax.setProjection("polar");
    EXPECT_EQ(ax.projection().kind, ProjectionKind::Polar);
    ax.setThetaZeroLocation("N");
    EXPECT_NEAR(ax.projection().thetaOffset, 1.5707963f, 1e-4f);
    ax.setThetaDirection(-1);
    EXPECT_NEAR(ax.projection().thetaDir, -1.0f, 1e-4f);
}

TEST(Projection, PolarTransformViewIsProjectedBounds) {
    Axes ax;
    ax.setProjection("polar");
    ax.setViewport({0.0f, 6.2831853f, 0.0f, 2.0f, 0, 1});
    auto t = ax.transform();
    // Polar bounds for rmax=2: [-2, 2] × [-2, 2].
    EXPECT_NEAR(t.view.x.min, -2.0f, 1e-4f);
    EXPECT_NEAR(t.view.x.max, 2.0f, 1e-4f);
    EXPECT_NEAR(t.view.y.min, -2.0f, 1e-4f);
    EXPECT_NEAR(t.view.y.max, 2.0f, 1e-4f);
    EXPECT_EQ(static_cast<ProjectionKind>(t.projection.kind), ProjectionKind::Polar);
}

// ─── Aspect / inversion ────────────────────────────────────────────────────

TEST(Aspect, EqualAspectShrinksBox) {
    Figure fig;
    auto* ax = fig.addAxes(0, 0);
    ax->setAspectEqual();
    // x span 1, y span 2 → data aspect 0.5; box must shrink horizontally.
    ax->setViewport({0, 1, 0, 2, 0, 1});
    fig.layout(Extent2D{400, 400});
    // With equal aspect and a square-ish grid cell, width < grid cell.
    EXPECT_LT(ax->rect.width, 400u);
}

TEST(Inversion, InvertXAxis) {
    Axes ax;
    ax.setViewport({0, 1, 0, 1, 0, 1});
    EXPECT_FALSE(ax.xAxisInverted());
    ax.invertXAxis();
    EXPECT_TRUE(ax.xAxisInverted());
    EXPECT_NEAR(ax.xlim().min, 1.0f, 1e-4f);
    EXPECT_NEAR(ax.xlim().max, 0.0f, 1e-4f);
}

TEST(Inversion, InvertYAxis) {
    Axes ax;
    ax.setViewport({0, 1, 0, 1, 0, 1});
    ax.invertYAxis();
    EXPECT_TRUE(ax.yAxisInverted());
}

// ─── Layout controls ───────────────────────────────────────────────────────

TEST(Layout, SubplotsAdjustSetsMargins) {
    Figure fig(1, 2);
    fig.subplotsAdjust(0.05f, 0.05f, 0.95f, 0.95f, 0.3f, 0.3f);
    EXPECT_NEAR(fig.grid().left, 0.05f, 1e-4f);
    EXPECT_NEAR(fig.grid().right, 0.95f, 1e-4f);
    EXPECT_NEAR(fig.grid().wspace, 0.3f, 1e-4f);
}

TEST(Layout, TightLayoutExpandsMargins) {
    Figure fig;
    auto* ax = fig.addAxes(0, 0);
    ax->style().xAxis.label = "X";
    ax->style().yAxis.label = "Y";
    fig.setTightLayout(true);
    fig.layout(Extent2D{400, 400});
    // Tight layout should reserve margin for labels → grid left > 0.
    EXPECT_GT(fig.grid().left, 0.01f);
    EXPECT_LT(fig.grid().right, 0.99f);
}

// ─── Rendering regressions (scale + projection) ────────────────────────────

namespace {
struct SFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;
    explicit SFig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.grid().left = 0.0f; figure.grid().right = 1.0f;
        figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
    }
    Image render() { return harness.render(figure); }
};
} // namespace

TEST(ScaleRegression, LogScaleRendersPointsAtDecades) {
    SFig cf(256);
    // Points at x = 1, 10, 100 on a log x axis.
    Series2D s;
    s.color = Color::red();
    s.marker = MarkerStyle::Circle;
    s.size = 6.0f;
    s.points = {{1.0f, 0.5f}, {10.0f, 0.5f}, {100.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    cf.axes->setViewport({1, 100, 0, 1, 0, 1});
    cf.axes->setXscale("log");
    auto img = cf.render();
    // x=10 (log10=1) should be near the horizontal center.
    // Find red pixels.
    uint32_t minX = 256, maxX = 0;
    for (uint32_t y = 0; y < 256; ++y)
        for (uint32_t x = 0; x < 256; ++x) {
            auto p = img.get(x, y);
            if (p.r > 200 && p.g < 80 && p.b < 80) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
            }
        }
    EXPECT_LE(minX, 30u);   // x=1 (log 0) near left edge of axes rect
    EXPECT_GE(maxX, 225u);  // x=100 (log 2) near right edge of axes rect
}

TEST(ProjectionRegression, PolarLineRenders) {
    SFig cf(256);
    // A polar "line": circle at r=1 sampled in theta.
    Series2D s;
    s.color = Color::red();
    for (int i = 0; i <= 64; ++i) {
        float th = float(i) / 64.0f * 2.0f * 3.14159265f;
        s.points.push_back({th, 1.0f});
    }
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setProjection("polar");
    cf.axes->setViewport({0.0f, 6.2832f, 0.0f, 1.0f, 0, 1});
    auto img = cf.render();
    // Should produce a ring of red pixels around the center.
    auto c = img.centroid(Pixel::red(), 40);
    EXPECT_GT(c.count, 20u);
    // Center should be near canvas center.
    EXPECT_NEAR(c.x, 128.0, 30.0);
    EXPECT_NEAR(c.y, 128.0, 30.0);
}
