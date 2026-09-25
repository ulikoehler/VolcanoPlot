// tests/test_parity18.cpp — tests for the axis/quiver/contour/table/
// backend_bases/bezier/mathtext parity batch:
//   1. ContourPlot::ensureLevels — mpl computes levels eagerly at
//      construction (levels populated before prepare()).
//   2. QuiverPlot::setUV — mpl Quiver.set_UVEC mutates the geometry.
//   3. TablePlot scaleX/scaleY — mpl Table.scale(w, h).
//   4. MathText symbol index — mpl mathtext.get_unicode_index.
//   5. AxLine accessors (vp.lines.AxLine introspection).
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/QuiverPlot.hpp>
#include <volcano/plot/plots/ReferenceLines.hpp>
#include <volcano/plot/Specialized.hpp>
#include <volcano/text/MathText.hpp>
#include "PlotTestHarness.hpp"

#include <gtest/gtest.h>

using namespace volcano;
using namespace volcano::plot;

namespace {

// ─── vp.contour — eager level computation ────────────────────────────

TEST(ContourParity, EnsureLevelsPopulatesAutoLevels) {
    Grid2D g;
    g.width = 4;
    g.height = 4;
    g.xRange = {0, 3};
    g.yRange = {0, 3};
    for (int i = 0; i < 16; ++i) g.values.push_back(float(i));
    ContourConfig cfg;
    cfg.numLevels = 5;
    ContourPlot p(g, cfg);
    EXPECT_TRUE(p.config().levels.empty());
    p.ensureLevels();
    // mpl MaxNLocator-style binning — non-empty, ascending.
    ASSERT_FALSE(p.config().levels.empty());
    for (size_t i = 1; i < p.config().levels.size(); ++i)
        EXPECT_GT(p.config().levels[i], p.config().levels[i - 1]);
}

TEST(ContourParity, EnsureLevelsKeepsExplicitLevels) {
    Grid2D g;
    g.width = 3;
    g.height = 3;
    for (int i = 0; i < 9; ++i) g.values.push_back(float(i));
    ContourConfig cfg;
    cfg.levels = {0.5f, 4.5f};
    ContourfPlot p(g, cfg);
    p.ensureLevels();
    ASSERT_EQ(p.config().levels.size(), 2u);
    EXPECT_FLOAT_EQ(p.config().levels[0], 0.5f);
    EXPECT_FLOAT_EQ(p.config().levels[1], 4.5f);
}

// ─── vp.quiver — set_UVEC mutates geometry ───────────────────────────

TEST(QuiverParity, SetUVReplacesVectorField) {
    QuiverPlot q({0, 1}, {0, 1}, {1, 1}, {0, 0}, {});
    q.setUV({2, 3}, {4, 5});
    // Positions are kept; u/v replaced and autoscale still valid.
    Viewport v;
    q.contributeToAutoscale(v);
    EXPECT_LE(v.x.min, 0.0f);
    EXPECT_GE(v.x.max, 1.0f);
}

// ─── vp.table — mpl Table.scale ──────────────────────────────────────

TEST(TableParity, ScaleXYFields) {
    TablePlot t{};
    EXPECT_FLOAT_EQ(t.scaleX, 1.0f);
    EXPECT_FLOAT_EQ(t.scaleY, 1.0f);
    t.scaleX = 1.5f;
    t.scaleY = 2.0f;
    EXPECT_FLOAT_EQ(t.scaleX, 1.5f);
    EXPECT_FLOAT_EQ(t.scaleY, 2.0f);
}

// ─── vp.mathtext — get_unicode_index ─────────────────────────────────

TEST(MathTextParity, SymbolIndex) {
    // mpl mathtext.get_unicode_index('alpha') == 0x3B1.
    EXPECT_EQ(volcano::text::mathSymbolIndex("alpha"), 0x03B1);
    EXPECT_EQ(volcano::text::mathSymbolIndex("beta"), 0x03B2);
    EXPECT_EQ(volcano::text::mathSymbolIndex("Gamma"), 0x0393);
    // mpl raises KeyError for unknown names — we return -1.
    EXPECT_EQ(volcano::text::mathSymbolIndex("not_a_symbol"), -1);
}

// ─── vp.lines — AxLine accessors ─────────────────────────────────────

TEST(LinesParity, AxLineAccessors) {
    AxLine l({1.0f, 2.0f}, {3.0f, 4.0f});
    EXPECT_FLOAT_EQ(l.xy1().x, 1.0f);
    EXPECT_FLOAT_EQ(l.xy1().y, 2.0f);
    EXPECT_FLOAT_EQ(l.slope(), 1.0f);
    l.setSlope(0.5f);
    EXPECT_FLOAT_EQ(l.slope(), 0.5f);
    // Vertical line → NaN slope (mpl semantics).
    l.setXy2({1.0f, 5.0f});
    EXPECT_TRUE(std::isnan(l.slope()));
}

} // namespace
