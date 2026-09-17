// tests/test_cycler.cpp — xkcd colors + property cycler tests
#include <gtest/gtest.h>
#include <volcano/plot/Cycler.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Rc.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>

using namespace volcano::plot;

struct RcGuard {
    ~RcGuard() { rc::rcdefaults(); }
};

// ─── xkcd color names ───────────────────────────────────────────────────────

TEST(XkcdColor, ParsesPrefixedName) {
    auto c = Color::parse("xkcd:cloudy blue");
    ASSERT_TRUE(c.has_value());
    // #acc2d9
    EXPECT_NEAR(c->r, 0xAC / 255.0f, 0.005f);
    EXPECT_NEAR(c->g, 0xC2 / 255.0f, 0.005f);
    EXPECT_NEAR(c->b, 0xD9 / 255.0f, 0.005f);
}

TEST(XkcdColor, CaseInsensitive) {
    auto a = Color::parse("xkcd:Sea Blue");
    auto b = Color::parse("XKCD:sea blue");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_FLOAT_EQ(a->r, b->r);
    EXPECT_FLOAT_EQ(a->g, b->g);
    EXPECT_FLOAT_EQ(a->b, b->b);
    // #047495
    EXPECT_NEAR(a->r, 0x04 / 255.0f, 0.005f);
    EXPECT_NEAR(a->b, 0x95 / 255.0f, 0.005f);
}

TEST(XkcdColor, UnprefixedXkcdNameNotFound) {
    // xkcd names live behind the xkcd: prefix — a survey name that isn't
    // also a CSS color must not resolve unprefixed.
    EXPECT_FALSE(Color::parse("cloudy blue").has_value());
}

TEST(XkcdColor, UnknownNameFails) {
    EXPECT_FALSE(Color::parse("xkcd:not a real color").has_value());
    EXPECT_FALSE(Color::parse("xkcd:").has_value());
}

TEST(XkcdColor, AlphaIsOpaque) {
    auto c = Color::parse("xkcd:dust");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->a, 1.0f);
}

// ─── Cycler basics ──────────────────────────────────────────────────────────

TEST(Cycler, SingleKeyColors) {
    auto c = Cycler::ofColors({Color::red(), Color::green(), Color::blue()});
    ASSERT_EQ(c.length(), 3u);
    EXPECT_FLOAT_EQ(c.at(0).color->r, 1.0f);
    EXPECT_FLOAT_EQ(c.at(1).color->g, 1.0f);
    // Wraps modulo length.
    EXPECT_FLOAT_EQ(c.at(3).color->r, 1.0f);
}

TEST(Cycler, NextAdvancesAndWraps) {
    auto c = Cycler::ofColors({Color::red(), Color::green()});
    EXPECT_FLOAT_EQ(c.next().color->r, 1.0f);
    EXPECT_FLOAT_EQ(c.next().color->g, 1.0f);
    EXPECT_FLOAT_EQ(c.next().color->r, 1.0f); // wrapped
    c.reset();
    EXPECT_FLOAT_EQ(c.next().color->r, 1.0f);
}

TEST(Cycler, PeekDoesNotAdvance) {
    auto c = Cycler::ofColors({Color::red(), Color::green()});
    EXPECT_FLOAT_EQ(c.peek().color->r, 1.0f);
    EXPECT_FLOAT_EQ(c.peek().color->r, 1.0f);
    c.advance();
    EXPECT_FLOAT_EQ(c.peek().color->g, 1.0f);
}

TEST(Cycler, Concatenation) {
    auto a = Cycler::ofColors({Color::red(), Color::green()});
    auto b = Cycler::ofColors({Color::blue()});
    auto c = a + b;
    ASSERT_EQ(c.length(), 3u);
    EXPECT_FLOAT_EQ(c.at(2).color->b, 1.0f);
}

TEST(Cycler, OuterProduct) {
    // matplotlib: cycler(color=[r,g]) * cycler(ls=[-,--]) has 4 entries.
    auto c = Cycler::ofColors({Color::red(), Color::green()}) *
             Cycler::ofLineStyles({LineStyle::Solid, LineStyle::Dashed});
    ASSERT_EQ(c.length(), 4u);
    // Outer loop = left cycler.
    EXPECT_FLOAT_EQ(c.at(0).color->r, 1.0f);
    EXPECT_EQ(c.at(0).lineStyle, LineStyle::Solid);
    EXPECT_FLOAT_EQ(c.at(1).color->r, 1.0f);
    EXPECT_EQ(c.at(1).lineStyle, LineStyle::Dashed);
    EXPECT_FLOAT_EQ(c.at(2).color->g, 1.0f);
    EXPECT_EQ(c.at(2).lineStyle, LineStyle::Solid);
    EXPECT_FLOAT_EQ(c.at(3).color->g, 1.0f);
    EXPECT_EQ(c.at(3).lineStyle, LineStyle::Dashed);
}

// ─── rc axes.prop_cycle multi-key parsing ───────────────────────────────────

TEST(RcPropCycle, MultiKeyZipped) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
        "cycler(color=['ff0000', '00ff00'], linestyle=['-', '--'])"));
    const auto& pc = rc::params().propCycle;
    ASSERT_EQ(pc.size(), 2u);
    ASSERT_TRUE(pc[0].color.has_value());
    EXPECT_NEAR(pc[0].color->r, 1.0f, 0.01f);
    EXPECT_EQ(pc[0].lineStyle, LineStyle::Solid);
    ASSERT_TRUE(pc[1].color.has_value());
    EXPECT_NEAR(pc[1].color->g, 1.0f, 0.01f);
    EXPECT_EQ(pc[1].lineStyle, LineStyle::Dashed);
}

TEST(RcPropCycle, ProductForm) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
        "cycler('color', ['r', 'g']) * cycler('linestyle', ['-', '--'])"));
    const auto& pc = rc::params().propCycle;
    ASSERT_EQ(pc.size(), 4u);
    EXPECT_EQ(pc[0].lineStyle, LineStyle::Solid);
    EXPECT_EQ(pc[1].lineStyle, LineStyle::Dashed);
    EXPECT_NEAR(pc[0].color->r, 1.0f, 0.01f);
    EXPECT_NEAR(pc[3].color->g, 0x80 / 255.0f, 0.01f); // 'g' = #008000
}

TEST(RcPropCycle, ColorOnlyStillMirrorsColorCycle) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
                        "cycler('color', ['3f90da', 'ffa90e'])"));
    ASSERT_EQ(rc::params().colorCycle.colors.size(), 2u);
    EXPECT_NEAR(rc::params().colorCycle.colors[0].r, 0x3F / 255.0f, 0.01f);
}

TEST(RcPropCycle, LinewidthAndMarkerKeys) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
        "cycler(lw=[1, 2], marker=['o', 's'])"));
    const auto& pc = rc::params().propCycle;
    ASSERT_EQ(pc.size(), 2u);
    EXPECT_FLOAT_EQ(pc[0].lineWidth.value_or(-1), 1.0f);
    EXPECT_FLOAT_EQ(pc[1].lineWidth.value_or(-1), 2.0f);
    EXPECT_EQ(pc[0].marker, MarkerStyle::Circle);
    EXPECT_EQ(pc[1].marker, MarkerStyle::Square);
}

TEST(RcPropCycle, SingleLengthListBroadcasts) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
        "cycler(color=['r', 'g'], linestyle=['-'])"));
    const auto& pc = rc::params().propCycle;
    ASSERT_EQ(pc.size(), 2u);
    EXPECT_EQ(pc[0].lineStyle, LineStyle::Solid);
    EXPECT_EQ(pc[1].lineStyle, LineStyle::Solid);
}

// ─── Axes prop_cycle application ────────────────────────────────────────────

TEST(AxesPropCycle, DefaultIsTab10) {
    Axes axes;
    ASSERT_EQ(axes.propCycle().length(), 10u);
    EXPECT_FLOAT_EQ(axes.propCycle().at(0).color->r, ColorCycle::at(0).r);
}

TEST(AxesPropCycle, OptInSeriesGetsCycleColor) {
    Axes axes;
    Series2D s1; s1.usePropCycle = true;
    Series2D s2; s2.usePropCycle = true;
    auto* p1 = static_cast<LinePlot*>(axes.addPlot(std::make_unique<LinePlot>(s1)));
    auto* p2 = static_cast<LinePlot*>(axes.addPlot(std::make_unique<LinePlot>(s2)));
    EXPECT_FLOAT_EQ(p1->series().color.r, ColorCycle::at(0).r);
    EXPECT_FLOAT_EQ(p2->series().color.r, ColorCycle::at(1).r);
}

TEST(AxesPropCycle, NonOptInSeriesKeepsColor) {
    Axes axes;
    Series2D s; // usePropCycle = false
    auto* p = static_cast<LinePlot*>(axes.addPlot(std::make_unique<LinePlot>(s)));
    EXPECT_FLOAT_EQ(p->series().color.b, Color::blue().b);
}

TEST(AxesPropCycle, NonConsumingPlotsDontAdvanceCycle) {
    Axes axes;
    // A plot type that doesn't consume the cycle (BarPlot uses BarData).
    Series2D s; s.usePropCycle = true;
    axes.addPlot(std::make_unique<LinePlot>(s));
    // Cycle advanced exactly once.
    EXPECT_FLOAT_EQ(axes.propCycle().peek().color->r, ColorCycle::at(1).r);
}

TEST(AxesPropCycle, MultiKeyCycleAppliesAllProps) {
    Axes axes;
    axes.setPropCycle(Cycler::ofColors({Color::red()}) *
                      Cycler::ofLineStyles({LineStyle::Dashed}));
    Series2D s; s.usePropCycle = true;
    auto* p = static_cast<LinePlot*>(axes.addPlot(std::make_unique<LinePlot>(s)));
    EXPECT_FLOAT_EQ(p->series().color.r, 1.0f);
    EXPECT_EQ(p->series().lineStyle, LineStyle::Dashed);
}

TEST(AxesPropCycle, MarkerCycledOnScatter) {
    Axes axes;
    axes.setPropCycle(Cycler::ofMarkers({MarkerStyle::Square, MarkerStyle::Diamond}));
    Series2D s1; s1.usePropCycle = true;
    Series2D s2; s2.usePropCycle = true;
    auto* p1 = static_cast<ScatterPlot*>(axes.addPlot(std::make_unique<ScatterPlot>(s1)));
    auto* p2 = static_cast<ScatterPlot*>(axes.addPlot(std::make_unique<ScatterPlot>(s2)));
    EXPECT_EQ(p1->series().marker, MarkerStyle::Square);
    EXPECT_EQ(p2->series().marker, MarkerStyle::Diamond);
}

TEST(AxesPropCycle, WrapsAroundAfterLength) {
    Axes axes;
    axes.setPropCycle(Cycler::ofColors({Color::red(), Color::green()}));
    for (int i = 0; i < 3; ++i) {
        Series2D s; s.usePropCycle = true;
        axes.addPlot(std::make_unique<LinePlot>(s));
    }
    // Third plot consumed red again; next entry is green.
    EXPECT_FLOAT_EQ(axes.propCycle().peek().color->g, Color::green().g);
}

TEST(AxesPropCycle, ResetRestartsCycle) {
    Axes axes;
    Series2D s; s.usePropCycle = true;
    axes.addPlot(std::make_unique<LinePlot>(s));
    axes.resetPropCycle();
    EXPECT_FLOAT_EQ(axes.propCycle().peek().color->r, ColorCycle::at(0).r);
}
