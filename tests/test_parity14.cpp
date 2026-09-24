// tests/test_parity14.cpp — tests for the ticker/introspection parity
// batch:
//   1. vp.ticker + tick introspection — mpl-exact locators
//      (MultipleLocator edge extension, IndexLocator alignment,
//      FixedLocator unclipped, AutoLocator steps + auto nbins,
//      AsinhLocator f64 params) and formatters (per-formatter
//      fix_minus, StrMethodFormatter format specs, PercentFormatter
//      auto decimals, EngFormatter places=None → %g).
//   2. TickConfig label color / hidden labels / rotation mode fields.
//   3. Axes default-locator/formatter bookkeeping + per-axis minor
//      tick control + inversion.
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/Scale.hpp>
#include <volcano/plot/Plot.hpp>
#include "../src/render/TickLayout.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace volcano;
using namespace volcano::plot;

namespace {

bool nearVec(std::vector<float> a, std::vector<double> b,
             double tol = 1e-4) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > tol * std::max(1.0, std::abs(b[i])))
            return false;
    return true;
}

} // namespace

// ─── Locator parity (values verified against matplotlib 3.10) ────────

TEST(TickerParity, MultipleLocatorExtendsBeyondView) {
    // mpl MultipleLocator(0.5).tick_values(-3, 5) → -3.5 .. 5.5.
    MultipleLocator loc{0.5f};
    auto t = loc.tickValues(-3.0f, 5.0f);
    ASSERT_EQ(t.size(), 19u);
    EXPECT_FLOAT_EQ(t.front(), -3.5f);
    EXPECT_FLOAT_EQ(t.back(), 5.5f);
}

TEST(TickerParity, MultipleLocatorOffset) {
    // mpl MultipleLocator(2, 1) on [0,7] → -1,1,3,5,7,9.
    MultipleLocator loc{2.0f, 1.0f};
    EXPECT_TRUE(nearVec(loc.tickValues(0.0f, 7.0f),
                        {-1, 1, 3, 5, 7, 9}));
}

TEST(TickerParity, IndexLocatorAlignsToVmin) {
    // mpl: arange(vmin + offset, vmax + 1, base) → -3,-1,1,3,5.
    IndexLocator loc{2.0f, 0.0f};
    EXPECT_TRUE(nearVec(loc.tickValues(-3.0f, 5.0f), {-3, -1, 1, 3, 5}));
    // offset shifts the starting point, not the grid.
    IndexLocator off{3.0f, 1.0f};
    EXPECT_TRUE(nearVec(off.tickValues(0.0f, 10.0f), {1, 4, 7, 10}));
}

TEST(TickerParity, FixedLocatorIsUnclipped) {
    // mpl FixedLocator ignores the view interval.
    FixedLocator loc{{1.0f, 9.0f, 100.0f}};
    EXPECT_TRUE(nearVec(loc.tickValues(0.0f, 50.0f), {1, 9, 100}));
}

TEST(TickerParity, MaxNLocatorDefaultNbinsIs10) {
    // mpl MaxNLocator default nbins=10 → step 1e4 on [0,1e5].
    MaxNLocator loc;
    auto t = loc.tickValues(0.0f, 1e5f);
    ASSERT_EQ(t.size(), 11u);
    EXPECT_FLOAT_EQ(t[1] - t[0], 1e4f);
}

TEST(TickerParity, AutoLocatorNarrowStaircase) {
    // mpl AutoLocator: steps [1,2,2.5,5,10] → step 2e4 on [0,1e5]
    // (nbins 'auto' → 9 without axis space information).
    AutoLocator loc;
    auto t = loc.tickValues(0.0f, 1e5f);
    ASSERT_FALSE(t.empty());
    EXPECT_FLOAT_EQ(t[1] - t[0], 2e4f);
}

TEST(TickerParity, AsinhLocatorDoubleParams) {
    // mpl AsinhLocator(0.2, 10).tick_values(-100, 100) →
    // [-10,-1,-0.1,0,0.1,1,10] — the ±100 endpoints drop out via fp.
    AsinhLocator loc{0.2, 10};
    EXPECT_TRUE(nearVec(loc.tickValues(-100.0f, 100.0f),
                        {-10, -1, -0.1, 0, 0.1, 1, 10}, 1e-3));
    // lw=1: the endpoint sinh/asinh roundtrip lands just above 100 →
    // pows 100 survive.
    AsinhLocator loc2{1.0, 11};
    EXPECT_TRUE(nearVec(loc2.tickValues(-100.0f, 100.0f),
                        {-100, -10, -1, 0, 1, 10, 100}, 1e-3));
}

// ─── Formatter parity ────────────────────────────────────────────────

TEST(TickerParity, FormatStrKeepsAsciiMinus) {
    // mpl FormatStrFormatter.__call__ bypasses fix_minus.
    FormatStrFormatter f{"%.2f"};
    EXPECT_FALSE(f.unicodeMinus());
    EXPECT_EQ(f.format(-1.5f, 0), "-1.50");
}

TEST(TickerParity, StrMethodFormatsSpec) {
    StrMethodFormatter f{"val={x:.2e}@{pos:02d}"};
    // fix_minus applies to the substituted value only.
    auto s = f.format(-0.25f, 3);
    EXPECT_NE(s.find("−2.50e−01"), std::string::npos);
    EXPECT_NE(s.find("@03"), std::string::npos);
    // Literal '-' in the template is NOT converted.
    StrMethodFormatter lit{"a-b={x:.1f}"};
    auto s2 = lit.format(-1.0f, 0);
    EXPECT_NE(s2.find("a-b="), std::string::npos);
    EXPECT_NE(s2.find("−1.0"), std::string::npos);
}

TEST(TickerParity, PercentFormatterAutoDecimals) {
    // mpl: decimals = clamp(ceil(2 - log10(2*scaled_range)), 0, 5).
    PercentFormatter f;  // xmax=100
    f.setViewInterval(0.0f, 2.0f);
    EXPECT_EQ(f.format(0.25f, 0), "0.25%");
    f.setViewInterval(0.0f, 0.005f);
    EXPECT_EQ(f.format(0.001f, 0), "0.0010%");
    f.setViewInterval(0.0f, 200.0f);
    EXPECT_EQ(f.format(34.5f, 0), "34%");
    // xmax=1: 0.25 maps to 25%; scaled range 200 → 0 decimals.
    PercentFormatter x1{1.0f};
    x1.setViewInterval(0.0f, 2.0f);
    EXPECT_EQ(x1.format(0.25f, 0), "25%");
}

TEST(TickerParity, EngFormatterGMode) {
    // mpl EngFormatter places=None → %g mantissa.
    EngFormatter f{"m"};
    EXPECT_EQ(f.format(1.25f, 0), "1.25 m");
    EXPECT_EQ(f.format(0.0f, 0), "0 m");
    // mpl joins sep+prefix+unit → '1.234 km'.
    EXPECT_EQ(f.format(1234.0f, 0), "1.234 km");
    EngFormatter f2{"Hz", 2};
    EXPECT_EQ(f2.format(0.0f, 0), "0.00 Hz");
    EXPECT_EQ(f2.format(0.25f, 0), "250.00 mHz");
    // 999.99 rounds to "999.99" (< 1000) → no rollover.
    EXPECT_EQ(f2.format(999.99f, 0), "999.99 Hz");
    // 999.999 rounds to "1000.00" (≥ 1000) → rolls into kHz.
    EXPECT_EQ(f2.format(999.999f, 0), "1.00 kHz");
}

TEST(TickerParity, UnicodeMinusFlagSet) {
    // Formatters whose mpl __call__ wraps fix_minus.
    EXPECT_TRUE(ScalarFormatter{}.unicodeMinus());
    EXPECT_TRUE(LogFormatter{}.unicodeMinus());
    EXPECT_TRUE(PercentFormatter{}.unicodeMinus());
    EXPECT_TRUE(EngFormatter{}.unicodeMinus());
    // Those that bypass it.
    EXPECT_FALSE(NullFormatter{}.unicodeMinus());
    EXPECT_FALSE(FormatStrFormatter{""}.unicodeMinus());
    EXPECT_FALSE(FixedFormatter{{}}.unicodeMinus());
    EXPECT_FALSE(StrMethodFormatter{""}.unicodeMinus());
}

// ─── TickConfig additions ────────────────────────────────────────────

TEST(TickConfigParity, LabelColorAndHiddenSets) {
    TickConfig tc;
    EXPECT_FALSE(tc.labelColor.has_value());
    tc.labelColor = Color{1, 0, 0, 1};
    tc.hiddenLabels.insert(1);
    tc.hiddenMinorLabels.insert(2);
    tc.labelRotationMode = "anchor";
    EXPECT_TRUE(tc.hiddenLabels.contains(1));
    EXPECT_TRUE(tc.hiddenMinorLabels.contains(2));
}

// ─── Axes tick state bookkeeping ─────────────────────────────────────

TEST(AxisStateParity, DefaultFlagsFlipOnSet) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    EXPECT_TRUE(ax->isDefaultMajLoc(true));
    EXPECT_TRUE(ax->isDefaultMajFmt(true));
    ax->setXLocator(std::make_shared<MultipleLocator>(0.5f));
    EXPECT_FALSE(ax->isDefaultMajLoc(true));
    EXPECT_TRUE(ax->isDefaultMajFmt(true));
    ax->setXFormatter(std::make_shared<FormatStrFormatter>("%.1f"));
    EXPECT_FALSE(ax->isDefaultMajFmt(true));
    EXPECT_TRUE(ax->isDefaultMajLoc(false));   // y untouched
}

TEST(AxisStateParity, SetInvertedPreservesDirection) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->setXlim(0.0f, 1.0f);
    ax->setXInverted(true);
    EXPECT_TRUE(ax->xAxisInverted());
    EXPECT_GT(ax->viewport().x.min, ax->viewport().x.max);
    // Bounds write keeps the inversion (mpl set_view_interval).
    ax->viewport().x = {2.0f, 0.0f};
    EXPECT_TRUE(ax->xAxisInverted());
}

TEST(AxisStateParity, PerAxisMinorTicks) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->minorticksOnAxis(true);
    EXPECT_TRUE(ax->style().xAxis.ticks.minor);
    EXPECT_FALSE(ax->style().yAxis.ticks.minor);
    ax->minorticksOnAxis(false);
    EXPECT_TRUE(ax->style().yAxis.ticks.minor);
    ax->minorticksOffAxis(false);
    EXPECT_FALSE(ax->style().yAxis.ticks.minor);
}

// ── Feature 2: Axes introspection / navigation parity ─────────────────

TEST(AxesIntrospection, EmptyDataLimIsInfinite) {
    // mpl: a fresh axes' dataLim is Bbox.null() = (inf, inf, -inf, -inf).
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    const auto& dl = ax->dataLim();
    EXPECT_TRUE(std::isinf(dl.x.min) && dl.x.min > 0);
    EXPECT_TRUE(std::isinf(dl.y.min) && dl.y.min > 0);
    EXPECT_TRUE(std::isinf(dl.x.max) && dl.x.max < 0);
    EXPECT_TRUE(std::isinf(dl.y.max) && dl.y.max < 0);
}

TEST(AxesIntrospection, PlotEagerlyUpdatesDataLim) {
    // mpl ax.plot merges the artist's extent into dataLim immediately.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->addPlot(std::make_unique<LinePlot>(
        Series2D{.points = {{0.0f, 0.0f}, {5.0f, 10.0f}}}));
    const auto& dl = ax->dataLim();
    EXPECT_FLOAT_EQ(dl.x.min, 0.0f);
    EXPECT_FLOAT_EQ(dl.x.max, 5.0f);
    EXPECT_FLOAT_EQ(dl.y.min, 0.0f);
    EXPECT_FLOAT_EQ(dl.y.max, 10.0f);
}

TEST(AxesIntrospection, UpdateDataLimMerges) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    Point2D pts[] = {{0, 0}, {20, 20}};
    ax->updateDataLim(pts);
    auto dl = ax->dataLim();
    EXPECT_FLOAT_EQ(dl.x.max, 20.0f);
    // updatex=False leaves the x extent untouched.
    Point2D pts2[] = {{100, 30}};
    ax->updateDataLim(pts2, /*updatex=*/false, /*updatey=*/true);
    dl = ax->dataLim();
    EXPECT_FLOAT_EQ(dl.x.max, 20.0f);
    EXPECT_FLOAT_EQ(dl.y.max, 30.0f);
}

TEST(AxesIntrospection, IgnoreExistingDataLimitsResetsMerge) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    Point2D pts[] = {{0, 0}, {20, 20}};
    ax->updateDataLim(pts);
    ax->ignoreExistingDataLimits = true;
    Point2D pts2[] = {{7, 8}};
    ax->updateDataLim(pts2);
    auto dl = ax->dataLim();
    // The prior (0..20) extent is forgotten — the flag resets the merge
    // base once (mpl ignore_existing_data_limits is consumed on use).
    EXPECT_FLOAT_EQ(dl.x.min, 7.0f);
    EXPECT_FLOAT_EQ(dl.x.max, 7.0f);
    EXPECT_FLOAT_EQ(dl.y.min, 8.0f);
    EXPECT_FLOAT_EQ(dl.y.max, 8.0f);
    EXPECT_FALSE(ax->ignoreExistingDataLimits);
}

TEST(AxesIntrospection, RelimRecomputesFromArtists) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    auto* lp = ax->addPlot(std::make_unique<LinePlot>(
        Series2D{.points = {{0.0f, 0.0f}, {5.0f, 10.0f}}}));
    ax->removePlot(lp);
    ax->relim();
    const auto& dl = ax->dataLim();
    EXPECT_TRUE(std::isinf(dl.x.min));  // empty again
}

TEST(AxesIntrospection, ArtistReattachKeepsData) {
    // mpl Artist.remove() detaches but keeps the artist alive;
    // add_line/addPlot re-attaches the same object.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    auto* lp = ax->addPlot(std::make_unique<LinePlot>(
        Series2D{.points = {{0, 0}, {1, 1}}}));
    auto detached = ax->removePlot(lp);
    ASSERT_NE(detached, nullptr);
    EXPECT_EQ(ax->plots().size(), 0u);
    EXPECT_EQ(ax->addPlot(std::move(detached)), lp);  // same object back
    EXPECT_EQ(ax->plots().size(), 1u);
}

TEST(AxesIntrospection, MarginsAndBoxAspect) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->margins(0.1f);
    EXPECT_FLOAT_EQ(ax->marginX(), 0.1f);
    EXPECT_FLOAT_EQ(ax->marginY(), 0.1f);
    ax->setXMargin(0.2f);
    EXPECT_FLOAT_EQ(ax->marginX(), 0.2f);
    EXPECT_FLOAT_EQ(ax->marginY(), 0.1f);  // mpl set_xmargin is x-only
    ax->setYMargin(0.3f);
    EXPECT_FLOAT_EQ(ax->marginY(), 0.3f);
    // mpl set_box_aspect flips adjustable to 'datalim'; clearing keeps
    // 'datalim' (mpl only sets adjustable when aspect is not None).
    ax->setBoxAspect(0.5f);
    EXPECT_EQ(ax->adjustable(), Adjustable::DataLim);
    ASSERT_TRUE(ax->boxAspect().has_value());
    EXPECT_FLOAT_EQ(*ax->boxAspect(), 0.5f);
    ax->setBoxAspect(std::nullopt);
    EXPECT_EQ(ax->adjustable(), Adjustable::DataLim);
}

TEST(AxesIntrospection, PanMovesView) {
    // mpl start_pan/drag_pan: dragging left moves the view right.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->rect = {0, 0, 400, 300};
    ax->setXlim(0.0f, 10.0f);
    ax->setYlim(0.0f, 5.0f);
    ax->startPan(200.0f, 150.0f);
    ax->dragPan(1, "", 150.0f, 150.0f);  // 50px left
    ax->endPan();
    EXPECT_NEAR(ax->viewport().x.min, 10.0f * 50.0f / 400.0f, 1e-4f);
    EXPECT_NEAR(ax->viewport().y.min, 0.0f, 1e-4f);
}

TEST(AxesIntrospection, ZoomDragShrinksLims) {
    // mpl button-3 drag up+right zooms in toward the start point.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->rect = {0, 0, 400, 300};
    ax->setXlim(0.0f, 10.0f);
    ax->setYlim(0.0f, 5.0f);
    ax->startPan(200.0f, 150.0f);
    ax->dragPan(3, "", 250.0f, 100.0f);  // 50px right, 50px up
    ax->endPan();
    EXPECT_LT(ax->viewport().x.span(), 10.0f);
    EXPECT_LT(ax->viewport().y.span(), 5.0f);
    EXPECT_GT(ax->viewport().x.min, 0.0f);
    EXPECT_GT(ax->viewport().y.min, 0.0f);
}

TEST(AxesIntrospection, NavigationAndArtistState) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    EXPECT_TRUE(ax->navigate());
    ax->setNavigate(false);
    EXPECT_FALSE(ax->navigate());
    ax->setNavigateMode("PAN");
    EXPECT_EQ(ax->navigateMode(), "PAN");
    ax->setVisible(false);
    EXPECT_FALSE(ax->visible());
    ax->setZorder(3.0f);
    EXPECT_FLOAT_EQ(ax->zorder(), 3.0f);
    ax->setGid("gid1");
    ax->setUrl("http://x");
    EXPECT_EQ(ax->gid(), "gid1");
    EXPECT_EQ(ax->url(), "http://x");
    ax->setPicker(5.0f);
    EXPECT_TRUE(ax->pickable());
    ax->setInLayout(false);
    EXPECT_FALSE(ax->inLayout());
}

TEST(AxesIntrospection, ContainsPointUsesRect) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->rect = {100, 50, 400, 300};
    EXPECT_TRUE(ax->containsPoint({200.0f, 100.0f}));
    EXPECT_TRUE(ax->containsPoint({100.0f, 50.0f}));   // corner inclusive
    EXPECT_FALSE(ax->containsPoint({99.0f, 100.0f}));
    EXPECT_FALSE(ax->containsPoint({200.0f, 400.0f}));
}

TEST(AxesIntrospection, SubplotSpecRoundTrip) {
    Figure figure{2, 2};
    auto* ax = figure.addAxes(0, 0);
    ASSERT_TRUE(ax->subplotSpec().has_value());
    EXPECT_EQ(ax->subplotSpec()->row, 0u);
    EXPECT_EQ(ax->subplotSpec()->col, 0u);
    auto* ax2 = figure.addAxes(1, 1);
    figure.setAxesSubplotSpec(ax2, ax->subplotSpec().value_or(SubplotSpec{}));
    ASSERT_TRUE(ax2->subplotSpec().has_value());
    EXPECT_EQ(ax2->subplotSpec()->row, 0u);
}

TEST(AxesIntrospection, DataRatioFromLimits) {
    // mpl get_data_ratio = yspan/xspan of the view limits.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    ax->setXlim(0.0f, 4.0f);
    ax->setYlim(0.0f, 2.0f);
    EXPECT_FLOAT_EQ(ax->dataRatio(), 0.5f);
}

// ── Feature 5: font properties + text kwargs round-trip ──
// The FontProperties struct is the C++ side of vp.font_manager's
// FontProperties; text structs carry the extra mpl kwargs as
// round-trip state.

TEST(FontParity, TextAnnotationFontFields) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    auto* t = ax->text(0.5f, 0.5f, "x");
    t->font.family = "serif";
    t->font.style = "italic";
    t->font.weight = "bold";
    t->usetex = true;
    t->wrap = true;
    t->rotationMode = "anchor";
    t->lineSpacing = 1.8f;
    t->multiAlign = HAlign::Left;
    t->backgroundColor = Color::fromRgba8(255, 255, 0);
    EXPECT_EQ(t->font.family, "serif");
    EXPECT_EQ(t->font.style, "italic");
    EXPECT_EQ(t->font.weight, "bold");
    EXPECT_TRUE(t->usetex);
    EXPECT_TRUE(t->wrap);
    EXPECT_EQ(t->rotationMode, "anchor");
    EXPECT_FLOAT_EQ(t->lineSpacing, 1.8f);
    ASSERT_TRUE(t->multiAlign.has_value());
    EXPECT_EQ(*t->multiAlign, HAlign::Left);
    EXPECT_GT(t->backgroundColor.a, 0.9f);
}

TEST(FontParity, AnnotationFontFields) {
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    auto* an = ax->annotate(0.1f, 0.1f, 0.2f, 0.2f, "a");
    an->font.weight = "demi";
    an->usetex = true;
    an->wrap = true;
    an->rotationMode = "anchor";
    an->lineSpacing = 2.0f;
    an->multiAlign = HAlign::Right;
    EXPECT_EQ(an->font.weight, "demi");
    EXPECT_TRUE(an->usetex);
    EXPECT_TRUE(an->wrap);
    EXPECT_EQ(an->rotationMode, "anchor");
    EXPECT_FLOAT_EQ(an->lineSpacing, 2.0f);
    ASSERT_TRUE(an->multiAlign.has_value());
    EXPECT_EQ(*an->multiAlign, HAlign::Right);
}

TEST(FontParity, StyleFontsCarryFamilyWeight) {
    // labelFont/tickFont/title.font/legend.font are FontProperties —
    // the same struct FontProperties kwargs write into.
    Figure figure{1, 1};
    auto* ax = figure.addAxes(0, 0);
    auto& st = ax->style();
    st.title.font.family = "serif";
    st.title.font.size = 18.0f;
    st.xAxis.labelFont.weight = "bold";
    st.xAxis.tickFont.family = "monospace";
    st.legend.font.size = 9.0f;
    st.legend.titleFont.weight = "bold";
    EXPECT_EQ(st.title.font.family, "serif");
    EXPECT_FLOAT_EQ(st.title.font.size, 18.0f);
    EXPECT_EQ(st.xAxis.labelFont.weight, "bold");
    EXPECT_EQ(st.xAxis.tickFont.family, "monospace");
    EXPECT_FLOAT_EQ(st.legend.font.size, 9.0f);
    EXPECT_EQ(st.legend.titleFont.weight, "bold");
}
