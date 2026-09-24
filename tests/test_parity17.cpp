// tests/test_parity17.cpp — tests for the pyplot/patches/hatch/path/
// layout_engine parity batch:
//   1. Path geometry (vp.path native side): unit factories, bounds,
//      flatten, containsPoint.
//   2. BoxStyle parsing + path generation (vp.patches depth).
//   3. hatchTriangles (vp.hatch native side): density + clipping.
//   4. Layout-engine params on plot::Figure (vp.layout_engine):
//      tightRect, tightPadScale, constrainedWSpace/HSpace.
//   5. ContourConfig::hatches field plumbing.
#include <volcano/plot/Path.hpp>
#include <volcano/plot/Collections.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/ContourPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include "PlotTestHarness.hpp"

#include <gtest/gtest.h>
#include <numeric>

using namespace volcano;
using namespace volcano::plot;

namespace {

// ─── vp.path native geometry ─────────────────────────────────────────

TEST(PathParity, UnitCircleBoundsAndContainment) {
    Path c = Path::unitCircle();
    auto [lo, hi] = c.bounds();
    EXPECT_NEAR(lo.x, -1.0f, 1e-4f);
    EXPECT_NEAR(lo.y, -1.0f, 1e-4f);
    EXPECT_NEAR(hi.x, 1.0f, 1e-4f);
    EXPECT_NEAR(hi.y, 1.0f, 1e-4f);
    EXPECT_TRUE(c.containsPoint({0, 0}));
    EXPECT_FALSE(c.containsPoint({2, 2}));
}

TEST(PathParity, UnitRectangleBounds) {
    // Native unitRectangle is origin-centered (the binding's mpl
    // unit_rectangle uses rectangle(0,0,1,1) instead).
    Path r = Path::unitRectangle();
    auto [lo, hi] = r.bounds();
    EXPECT_NEAR(lo.x, -0.5f, 1e-6f);
    EXPECT_NEAR(lo.y, -0.5f, 1e-6f);
    EXPECT_NEAR(hi.x, 0.5f, 1e-6f);
    EXPECT_NEAR(hi.y, 0.5f, 1e-6f);
    // mpl unit_rectangle semantics via rectangle(0,0,1,1): closed,
    // 5 verts, bounds [0,1].
    Path mr = Path::rectangle(0, 0, 1, 1);
    EXPECT_EQ(mr.vertices.size(), 5u);
    auto [mlo, mhi] = mr.bounds();
    EXPECT_NEAR(mlo.x, 0.0f, 1e-6f);
    EXPECT_NEAR(mhi.x, 1.0f, 1e-6f);
}

TEST(PathParity, FlattenProducesPolyline) {
    Path c = Path::unitCircle();
    auto flat = c.flatten();
    ASSERT_GT(flat.size(), 4u);
    // Every flattened vertex lies within the unit bounds.
    for (auto p : flat) {
        EXPECT_GE(p.x, -1.001f);
        EXPECT_LE(p.x, 1.001f);
        EXPECT_GE(p.y, -1.001f);
        EXPECT_LE(p.y, 1.001f);
    }
}

// ─── vp.patches BoxStyle ─────────────────────────────────────────────

TEST(BoxStyleParity, ParseRound) {
    auto s = parseBoxStyle("round,pad=0.5");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->kind, BoxStyleSpec::Kind::Round);
    EXPECT_FLOAT_EQ(s->pad, 0.5f);
}

TEST(BoxStyleParity, ParseParams) {
    auto s = parseBoxStyle("sawtooth,pad=0.3,tooth_size=0.2");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->kind, BoxStyleSpec::Kind::Sawtooth);
    EXPECT_FLOAT_EQ(s->toothSize, 0.2f);
    // Unknown names are rejected like mpl's _style_list KeyError.
    EXPECT_FALSE(parseBoxStyle("nonexistent").has_value());
}

TEST(BoxStyleParity, PathExpandsRect) {
    auto s = parseBoxStyle("round,pad=0.3");
    ASSERT_TRUE(s.has_value());
    Path p = boxStylePath(0, 0, 10, 10, *s);
    ASSERT_GT(p.vertices.size(), 4u);
    auto [lo, hi] = p.bounds();
    // mpl: pad grows the box beyond the rect in every direction.
    EXPECT_LT(lo.x, 0.0f);
    EXPECT_LT(lo.y, 0.0f);
    EXPECT_GT(hi.x, 10.0f);
    EXPECT_GT(hi.y, 10.0f);
}

// ─── vp.hatch native hatchTriangles ──────────────────────────────────

TEST(HatchParity, DensityRepeat) {
    std::vector<Point2D> rect = {
        {0, 0}, {100, 0}, {100, 100}, {0, 100}};
    auto one = hatchTriangles(rect, "/", 12.0f);
    auto three = hatchTriangles(rect, "///", 12.0f);
    ASSERT_GT(one.size(), 0u);
    // Repeating the pattern char increases line density (mpl rule).
    EXPECT_GT(three.size(), one.size() * 2);
    // All segments stay inside the polygon bounds.
    for (auto p : one) {
        EXPECT_GE(p.x, -0.5f);
        EXPECT_LE(p.x, 100.5f);
        EXPECT_GE(p.y, -0.5f);
        EXPECT_LE(p.y, 100.5f);
    }
}

TEST(HatchParity, AnchorConsistency) {
    // The anchor keeps hatch phases aligned across disjoint fragments
    // of the same band (contourf): same anchor → identical geometry
    // for the same polygon regardless of where it sits.
    std::vector<Point2D> a = {{0, 0}, {50, 0}, {50, 50}, {0, 50}};
    std::vector<Point2D> b = {{60, 60}, {110, 60}, {110, 110}, {60, 110}};
    auto ha = hatchTriangles(a, "|", 10.0f, Point2D{0, 0});
    auto hb = hatchTriangles(b, "|", 10.0f, Point2D{0, 0});
    ASSERT_GT(ha.size(), 0u);
    ASSERT_GT(hb.size(), 0u);
    // Anchored hatches share x phases: line *centers* land on
    // multiples of the spacing from the anchor; quad vertices are
    // offset by half the line width (0.5) either side.
    for (auto p : ha) {
        float m = std::fmod(p.x - 0.0f, 10.0f);
        EXPECT_TRUE(m < 0.6f || m > 9.4f) << "x=" << p.x;
    }
}

// ─── vp.layout_engine native params ──────────────────────────────────

TEST(LayoutEngineParity, TightRectClampsMargins) {
    Figure fig;
    Axes* ax = fig.addAxes(0, 0);
    Series2D s; s.points = {{0, 0}, {1, 1}};
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    fig.setTightLayout(true);
    // mpl TightLayoutEngine rect=(0.2, 0.2, 0.9, 0.9): the subplot
    // region must fit inside it.
    fig.tightRect = {0.2f, 0.2f, 0.9f, 0.9f};
    fig.layout({400, 400});
    EXPECT_GE(fig.grid().left, 0.2f - 1e-6f);
    EXPECT_GE(fig.grid().bottom, 0.2f - 1e-6f);
    EXPECT_LE(fig.grid().right, 0.9f + 1e-6f);
    EXPECT_LE(fig.grid().top, 0.9f + 1e-6f);
}

TEST(LayoutEngineParity, TightPadScaleGrowsMargins) {
    Figure a, b;
    Series2D sa; sa.points = {{0, 0}, {1, 1}};
    Series2D sb; sb.points = {{0, 0}, {1, 1}};
    a.addAxes(0, 0)->addPlot(std::make_unique<LinePlot>(std::move(sa)));
    b.addAxes(0, 0)->addPlot(std::make_unique<LinePlot>(std::move(sb)));
    a.setTightLayout(true);
    b.setTightLayout(true);
    b.tightPadScale = 2.0f;
    a.layout({400, 400});
    b.layout({400, 400});
    EXPECT_GT(b.grid().left, a.grid().left);
    EXPECT_GT(b.grid().bottom, a.grid().bottom);
}

TEST(LayoutEngineParity, ConstrainedSpacingParam) {
    Figure fig;
    fig.addAxes(0, 0);
    fig.addAxes(0, 1);
    fig.setConstrainedLayout(true);
    fig.constrainedWSpace = 0.35f;
    fig.constrainedHSpace = 0.35f;
    fig.layout({400, 400});
    // mpl: engine wspace/hspace raise the inter-axes gap floor.
    EXPECT_GE(fig.grid().wspace, 0.35f - 1e-6f);
    EXPECT_GE(fig.grid().hspace, 0.35f - 1e-6f);
}

// ─── contourf hatches plumbing ───────────────────────────────────────

TEST(ContourHatchParity, ConfigField) {
    ContourConfig cfg;
    cfg.hatches = {"/", "x", "."};
    EXPECT_EQ(cfg.hatches.size(), 3u);
    EXPECT_EQ(cfg.hatches[1], "x");
}

} // namespace
