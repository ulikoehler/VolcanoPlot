// tests/test_parity15.cpp — tests for the scale/units/animation/sankey/
// colorbar parity batch:
//   1. vp.scale — log base kwarg (AxisScale::log(2) ticks at 2^k),
//      SymmetricalLogLocator rewritten to mpl's a/b/c algorithm
//      (decades + lone 0, stride, subs), LogFormatterMathtext/
//      LogFormatterSciNotation negative decades (-10^{0}).
//   2. vp.sankey — Sankey::finish() returns mpl-shaped Diagram
//      metadata (flows, angles, tips, patch/text/texts handles).
//   3. vp.colorbar — ColorbarStyle::caxMode (strip fills the cax
//      axes rect), format=/extendfrac/extendrect fields.
#include <volcano/plot/Scale.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/Specialized.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include "../src/render/TickLayout.hpp"
#include "PlotTestHarness.hpp"

#include <gtest/gtest.h>

using namespace volcano;
using namespace volcano::plot;

namespace {

// ─── 1. vp.scale ────────────────────────────────────────────────────────────

TEST(ScaleParity, LogBase2TicksAtPowers) {
    // mpl LogLocator(base=2): ticks are powers of 2 (strided when the
    // decade count exceeds the tick space).
    AxisScale s = AxisScale::log(2.0f);
    auto t = scaleTicks(s, 0.8f, 300.0f, 9);
    ASSERT_GE(t.size(), 2u);
    for (float v : t) {
        double lg = std::log2(double(v));
        EXPECT_NEAR(lg, std::round(lg), 1e-5) << v << " not a power of 2";
    }
}

TEST(ScaleParity, LogBaseStoredInParam1) {
    // param1 = tick base; param3 = 10 keeps the GLSL transform a pure
    // log (all log bases are display-space scalar multiples).
    AxisScale s = AxisScale::log(2.0f);
    EXPECT_FLOAT_EQ(s.param1, 2.0f);
    EXPECT_FLOAT_EQ(s.param3, 10.0f);
    // nonpositive='mask' flag in param2.
    AxisScale m = AxisScale::log(10.0f, true);
    EXPECT_FLOAT_EQ(m.param2, 1.0f);
}

TEST(ScaleParity, SymlogLocatorMatchesMpl) {
    // mpl SymmetricalLogLocator(linthresh=2, base=10) on [-130,130]:
    // [-100, -10, -1, 0, 1, 10, 100] — decades + a lone 0.
    SymmetricalLogLocator loc(2.0f);
    auto t = loc.tickValues(-130.0f, 130.0f);
    std::vector<float> want{-100, -10, -1, 0, 1, 10, 100};
    ASSERT_EQ(t.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i)
        EXPECT_NEAR(t[i], want[i], 1e-4f);
}

TEST(ScaleParity, SymlogLocatorLinearOnlyRange) {
    // mpl "simple" mode: range inside [-linthresh, linthresh] →
    // (vmin, 0, vmax).
    SymmetricalLogLocator loc(2.0f);
    auto t = loc.tickValues(-1.5f, 1.0f);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_FLOAT_EQ(t[0], -1.5f);
    EXPECT_FLOAT_EQ(t[1], 0.0f);
    EXPECT_FLOAT_EQ(t[2], 1.0f);
}

TEST(ScaleParity, SymlogLocatorSubsMultiplyDecades) {
    // mpl minor locator: subs=[2..9] → ticks at sub*base^e (plus the
    // lone 0 of the linear segment).
    std::vector<float> subs{2, 3, 4, 5, 6, 7, 8, 9};
    SymmetricalLogLocator loc(2.0f, 10.0f, subs);
    auto t = loc.tickValues(0.0f, 200.0f);
    auto has = [&](float v) {
        for (float x : t) if (std::abs(x - v) < 1e-3f) return true;
        return false;
    };
    EXPECT_TRUE(has(0.0f));
    EXPECT_TRUE(has(2.0f));
    EXPECT_TRUE(has(9.0f));
    EXPECT_TRUE(has(20.0f));
    EXPECT_TRUE(has(90.0f));
    EXPECT_TRUE(has(200.0f));
    EXPECT_TRUE(has(900.0f));
    EXPECT_FALSE(has(1.0f));
    EXPECT_FALSE(has(100.0f));
}

TEST(ScaleParity, SymlogAxisUsesScaleTicksLocator) {
    // scaleTicks for a symlog AxisScale delegates to
    // SymmetricalLogLocator (mpl set_default_locators_and_formatters).
    AxisScale s = AxisScale::symlog(2.0f, 1.0f, 10.0f);
    auto t = scaleTicks(s, -130.0f, 130.0f, 9);
    auto has = [&](float v) {
        for (float x : t) if (std::abs(x - v) < 1e-3f) return true;
        return false;
    };
    EXPECT_TRUE(has(-100.0f));
    EXPECT_TRUE(has(0.0f));
    EXPECT_TRUE(has(100.0f));
    EXPECT_FALSE(has(2.0f));   // linthresh is not a major tick in mpl
    EXPECT_FALSE(has(-2.0f));
}

TEST(ScaleParity, LogMathtextNegativeDecade) {
    // mpl LogFormatter.format_data(-1, base=10) → "$-10^{0}$".
    LogFormatterMathtext f(10.0f);
    EXPECT_EQ(f.format(-1.0f, 0), "$-10^{0}$");
    EXPECT_EQ(f.format(-100.0f, 0), "$-10^{2}$");
    EXPECT_EQ(f.format(100.0f, 0), "$10^{2}$");
    EXPECT_EQ(f.format(0.0f, 0), "0");
}

TEST(ScaleParity, LogSciNotationNegativeDecade) {
    LogFormatterSciNotation f(10.0f);
    EXPECT_EQ(f.format(-10.0f, 0), "$-10^{1}$");
    EXPECT_EQ(f.format(0.0f, 0), "0");
}

// ─── 2. vp.sankey ───────────────────────────────────────────────────────────

TEST(SankeyParity, FinishReturnsDiagramMetadata) {
    Figure fig(1, 1);
    auto* ax = fig.addAxes();
    Sankey sk(*ax);
    sk.patchLabels = {"Sys"};
    sk.add({1.0f, 0.5f, -1.5f}, {"in1", "in2", "out"});
    auto diagrams = sk.finish();
    ASSERT_EQ(diagrams.size(), 1u);
    const auto& d = diagrams[0];
    ASSERT_EQ(d.flows.size(), 3u);
    EXPECT_FLOAT_EQ(d.flows[0], 1.0f);
    EXPECT_FLOAT_EQ(d.flows[2], -1.5f);
    // mpl angles: one entry per flow.
    ASSERT_EQ(d.angles.size(), 3u);
    // mpl tips: one Point2D per flow.
    ASSERT_EQ(d.tips.size(), 3u);
    // Trunk patch handle is set; patchlabel text exists.
    EXPECT_NE(d.patch, nullptr);
    EXPECT_NE(d.text, nullptr);
    // mpl texts: one handle per flow label.
    ASSERT_EQ(d.texts.size(), 3u);
    EXPECT_NE(d.texts[0], nullptr);
    EXPECT_NE(d.texts[2], nullptr);
    // Ribbons: one patch per flow.
    EXPECT_EQ(d.ribbons.size(), 3u);
}

TEST(SankeyParity, SubToleranceFlowHasNullAngle) {
    Figure fig(1, 1);
    auto* ax = fig.addAxes();
    Sankey sk(*ax);
    sk.tolerance = 0.1f;
    sk.add({1.0f, 1e-9f, -1.0f});
    auto diagrams = sk.finish();
    ASSERT_EQ(diagrams.size(), 1u);
    ASSERT_EQ(diagrams[0].angles.size(), 3u);
    // The ~0 flow is skipped like mpl (angle = None).
    EXPECT_FALSE(diagrams[0].angles[1].has_value());
    EXPECT_TRUE(diagrams[0].angles[0].has_value());
}

// ─── 3. vp.colorbar ─────────────────────────────────────────────────────────

TEST(ColorbarParity, CaxModeFillsAxesRect) {
    test::PlotTestHarness h{256, 256};
    Figure fig(1, 1);
    auto* ax = fig.addAxes();
    Grid2D g;
    g.width = 4; g.height = 4;
    g.xRange = {0, 4}; g.yRange = {0, 4};
    g.values.assign(16, 5.0f);
    ax->addPlot(std::make_unique<HeatmapPlot>(std::move(g)));
    auto* cax = fig.addAxesFraction(0.9f, 0.1f, 0.06f, 0.8f);
    auto& cb = cax->style().colorbar;
    cb.visible = true;
    cb.caxMode = true;
    cb.cmapPtr = &Colormap::byName("viridis");
    cb.explicitRange = Range{0.0f, 10.0f};
    auto img = h.render(fig);
    // The cax region should contain viridis strip colors (non-white).
    uint32_t x0 = uint32_t(0.905f * 256), x1 = uint32_t(0.955f * 256);
    uint32_t y0 = uint32_t(0.15f * 256), y1 = uint32_t(0.85f * 256);
    int colored = 0;
    for (uint32_t y = y0; y < y1; ++y)
        for (uint32_t x = x0; x < x1; ++x) {
            auto p = img.get(x, 255 - y);
            if (p.r < 240 || p.g < 240 || p.b < 240) ++colored;
        }
    EXPECT_GT(colored, int((x1 - x0) * (y1 - y0) / 3));
}

TEST(ColorbarParity, StyleDefaultsMatchMpl) {
    ColorbarStyle cb;
    EXPECT_EQ(cb.orientation, "vertical");
    EXPECT_FLOAT_EQ(cb.fraction, 0.15f);   // mpl fraction=0.15
    EXPECT_FLOAT_EQ(cb.pad, 0.05f);        // mpl pad=0.05
    EXPECT_FLOAT_EQ(cb.shrink, 1.0f);      // mpl shrink=1.0
    EXPECT_FLOAT_EQ(cb.aspect, 20.0f);     // mpl aspect=20
    EXPECT_EQ(cb.extend, "neither");
    EXPECT_FLOAT_EQ(cb.alpha, 1.0f);
    EXPECT_EQ(cb.ticklocation, "auto");
    EXPECT_EQ(cb.spacing, "uniform");
    EXPECT_LT(cb.extendfrac, 0.0f);        // <=0 → auto 0.05
    EXPECT_FALSE(cb.extendrect);
    EXPECT_FALSE(cb.drawedges);
    EXPECT_FALSE(cb.caxMode);
}

TEST(ColorbarParity, FormatStringOverridesLabels) {
    auto set = render::colorbarTicks({}, {}, false, nullptr,
                                     0.0f, 10.0f, "%.1f");
    ASSERT_FALSE(set.labels.empty());
    for (auto& l : set.labels)
        EXPECT_NE(l.find('.'), std::string::npos)
            << "label '" << l << "' should carry a decimal point";
}

} // namespace
