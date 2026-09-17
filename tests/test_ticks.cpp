// tests/test_ticks.cpp — §9 tick locators, formatters, minor ticks
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <cmath>
#include <memory>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

// ═══ Locators ═══════════════════════════════════════════════════════════════

TEST(TickLocators, NullLocatorEmpty) {
    NullLocator loc;
    EXPECT_TRUE(loc.tickValues(0.0f, 10.0f).empty());
}

TEST(TickLocators, FixedLocatorFiltersRange) {
    FixedLocator loc({1.0f, 5.0f, 20.0f});
    auto t = loc.tickValues(0.0f, 10.0f);
    ASSERT_EQ(t.size(), 2u);
    EXPECT_FLOAT_EQ(t[0], 1.0f);
    EXPECT_FLOAT_EQ(t[1], 5.0f);
}

TEST(TickLocators, LinearLocatorInclusiveEndpoints) {
    LinearLocator loc(5);
    auto t = loc.tickValues(0.0f, 1.0f);
    ASSERT_EQ(t.size(), 5u);
    EXPECT_FLOAT_EQ(t.front(), 0.0f);
    EXPECT_FLOAT_EQ(t.back(), 1.0f);
    EXPECT_NEAR(t[2], 0.5f, 1e-6f);
}

TEST(TickLocators, MultipleLocatorBase) {
    MultipleLocator loc(0.5f);
    auto t = loc.tickValues(0.0f, 1.0f);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_FLOAT_EQ(t[0], 0.0f);
    EXPECT_FLOAT_EQ(t[1], 0.5f);
    EXPECT_FLOAT_EQ(t[2], 1.0f);
}

TEST(TickLocators, MultipleLocatorOffset) {
    MultipleLocator loc(2.0f, 1.0f); // odd integers
    auto t = loc.tickValues(0.0f, 5.0f);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_FLOAT_EQ(t[0], 1.0f);
    EXPECT_FLOAT_EQ(t[1], 3.0f);
    EXPECT_FLOAT_EQ(t[2], 5.0f);
}

TEST(TickLocators, IndexLocatorIntegerSteps) {
    IndexLocator loc(1.0f, 0.5f);
    auto t = loc.tickValues(0.0f, 3.0f);
    ASSERT_EQ(t.size(), 3u); // 0.5, 1.5, 2.5 (3.5 out of range)
    EXPECT_FLOAT_EQ(t[0], 0.5f);
    EXPECT_FLOAT_EQ(t[2], 2.5f);
}

TEST(TickLocators, MaxNLocatorNiceSteps) {
    MaxNLocator loc(9);
    auto t = loc.tickValues(0.0f, 10.0f);
    EXPECT_GE(t.size(), 5u);
    EXPECT_LE(t.size(), 11u);
    // Steps should be nice numbers (1, 2, 2.5, 5 × 10^k).
    float step = t[1] - t[0];
    EXPECT_TRUE(step == 1.0f || step == 2.0f || step == 2.5f ||
                step == 5.0f) << "step=" << step;
}

TEST(TickLocators, AutoLocatorDefaultsToNine) {
    AutoLocator loc;
    auto t = loc.tickValues(0.0f, 100.0f);
    EXPECT_GE(t.size(), 5u);
    EXPECT_LE(t.size(), 11u);
}

TEST(TickLocators, LogLocatorDecades) {
    LogLocator loc;
    auto t = loc.tickValues(1.0f, 1000.0f);
    ASSERT_EQ(t.size(), 4u);
    EXPECT_FLOAT_EQ(t[0], 1.0f);
    EXPECT_FLOAT_EQ(t[3], 1000.0f);
}

TEST(TickLocators, LogLocatorMinorSubs) {
    LogLocator loc;
    auto m = loc.minorValues(1.0f, 100.0f);
    // subs 2..9 in each of two decades → 16 minors.
    EXPECT_EQ(m.size(), 16u);
    // Sorted and inside range.
    for (float v : m) {
        EXPECT_GE(v, 1.0f);
        EXPECT_LE(v, 100.0f);
    }
    // First decade: 2,3,...,9.
    EXPECT_FLOAT_EQ(m[0], 2.0f);
    EXPECT_FLOAT_EQ(m[7], 9.0f);
}

TEST(TickLocators, LogLocatorStridesManyDecades) {
    LogLocator loc(10.0f, 9);
    auto t = loc.tickValues(1.0f, 1e12f);
    // 13 decades > numticks → strided.
    EXPECT_LE(t.size(), 9u);
    EXPECT_GE(t.size(), 5u);
}

TEST(TickLocators, SymmetricalLogLocatorDecades) {
    SymmetricalLogLocator loc(2.0f);
    auto t = loc.tickValues(-1000.0f, 1000.0f);
    // Should include ±10, ±100, ±1000 plus linear ticks near 0.
    auto has = [&](float v) {
        for (float x : t) if (std::abs(x - v) < 1e-3f) return true;
        return false;
    };
    EXPECT_TRUE(has(100.0f));
    EXPECT_TRUE(has(-100.0f));
    EXPECT_TRUE(has(1000.0f));
    EXPECT_TRUE(has(0.0f));
}

TEST(TickLocators, LogitLocatorCanonicalProbs) {
    LogitLocator loc;
    auto t = loc.tickValues(0.001f, 0.999f);
    auto has = [&](float v) {
        for (float x : t) if (std::abs(x - v) < 1e-5f) return true;
        return false;
    };
    EXPECT_TRUE(has(0.5f));
    EXPECT_TRUE(has(0.9f));
    EXPECT_TRUE(has(0.1f));
    EXPECT_TRUE(has(0.99f));
    EXPECT_TRUE(has(0.01f));
}

TEST(TickLocators, AutoMinorLocatorSubdivides) {
    AutoMinorLocator loc(5);
    std::vector<float> majors = {0.0f, 1.0f, 2.0f};
    auto m = loc.between(majors, 0.0f, 2.0f);
    ASSERT_EQ(m.size(), 8u); // 4 per interval × 2
    EXPECT_FLOAT_EQ(m[0], 0.2f);
    EXPECT_FLOAT_EQ(m[3], 0.8f);
}

TEST(TickLocators, AutoMinorLocatorStandalone) {
    AutoMinorLocator loc;
    auto m = loc.tickValues(0.0f, 10.0f);
    EXPECT_GT(m.size(), 10u); // subdivisions of ~9 majors
}

// ═══ Formatters ═════════════════════════════════════════════════════════════

TEST(TickFormatters, NullFormatterEmpty) {
    NullFormatter f;
    EXPECT_TRUE(f.format(1.5f, 0).empty());
}

TEST(TickFormatters, FixedFormatterByIndex) {
    FixedFormatter f({"a", "b"});
    EXPECT_EQ(f.format(0.0f, 0), "a");
    EXPECT_EQ(f.format(99.0f, 1), "b");
    EXPECT_TRUE(f.format(0.0f, 5).empty());
}

TEST(TickFormatters, FuncFormatterCallback) {
    FuncFormatter f([](float v, int) {
        return std::to_string(int(v * 2));
    });
    EXPECT_EQ(f.format(1.5f, 0), "3");
}

TEST(TickFormatters, FormatStrFormatter) {
    FormatStrFormatter f("%.2f");
    EXPECT_EQ(f.format(1.2345f, 0), "1.23");
}

TEST(TickFormatters, StrMethodFormatter) {
    StrMethodFormatter f("{x} cm");
    EXPECT_EQ(f.format(2.5f, 0), "2.5 cm");
    StrMethodFormatter p("[{pos}]");
    EXPECT_EQ(p.format(9.0f, 3), "[3]");
}

TEST(TickFormatters, ScalarFormatterPlain) {
    ScalarFormatter f;
    float locs[] = {0.0f, 0.5f, 1.0f};
    f.setLocs(locs);
    EXPECT_EQ(f.format(0.5f, 1), "0.5");
    EXPECT_TRUE(f.offsetText().empty());
}

TEST(TickFormatters, ScalarFormatterSciLimits) {
    ScalarFormatter f;
    f.scilimits = {-3, 3};
    float locs[] = {0.0f, 5000.0f, 10000.0f};
    f.setLocs(locs);
    // oom=4 ≥ 3 → scientific.
    EXPECT_EQ(f.format(5000.0f, 1), "0.5");
    EXPECT_EQ(f.offsetText(), "1e4");
}

TEST(TickFormatters, ScalarFormatterMathText) {
    ScalarFormatter f;
    f.scilimits = {-3, 3};
    f.useMathText = true;
    float locs[] = {0.0f, 1e6f};
    f.setLocs(locs);
    EXPECT_EQ(f.offsetText(), "$10^{6}$");
}

TEST(TickFormatters, ScalarFormatterOffset) {
    ScalarFormatter f;
    f.scilimits = {-7, 7};
    float locs[] = {20001.0f, 20002.0f, 20003.0f};
    f.setLocs(locs);
    EXPECT_FALSE(f.offsetText().empty());
    // Labels are v - offset.
    EXPECT_EQ(f.format(20001.0f, 0), "1");
}

TEST(TickFormatters, ScalarFormatterNoOffset) {
    ScalarFormatter f;
    f.useOffset = false;
    float locs[] = {20001.0f, 20002.0f, 20003.0f};
    f.setLocs(locs);
    EXPECT_TRUE(f.offsetText().empty());
    EXPECT_EQ(f.format(20001.0f, 0), "20001");
}

TEST(TickFormatters, ScalarFormatterForceSci) {
    ScalarFormatter f;
    f.forceSci = true;
    float locs[] = {10.0f, 20.0f};
    f.setLocs(locs);
    EXPECT_EQ(f.format(20.0f, 1), "2");
    EXPECT_EQ(f.offsetText(), "1e1");
}

TEST(TickFormatters, LogFormatterPowers) {
    LogFormatter f;
    EXPECT_EQ(f.format(100.0f, 0), "$10^{2}$");
    EXPECT_EQ(f.format(0.1f, 0), "$10^{-1}$");
    EXPECT_EQ(f.format(5.0f, 0), "5"); // not a power → %g
}

TEST(TickFormatters, LogFormatterExponentAlways) {
    LogFormatterExponent f;
    EXPECT_EQ(f.format(50.0f, 0), "$10^{2}$"); // rounded exponent
}

TEST(TickFormatters, LogFormatterMathtextBase) {
    LogFormatterMathtext f(2.0f);
    EXPECT_EQ(f.format(8.0f, 0), "$2^{3}$");
}

TEST(TickFormatters, LogFormatterSciNotation) {
    LogFormatterSciNotation f;
    EXPECT_EQ(f.format(300.0f, 0), "$3\\times10^{2}$");
    EXPECT_EQ(f.format(3.5f, 0), "$3.5\\times10^{0}$");
}

TEST(TickFormatters, LogitFormatterProbs) {
    LogitFormatter f;
    EXPECT_EQ(f.format(0.5f, 0), "0.5");
    EXPECT_EQ(f.format(0.99f, 0), "0.99");
    EXPECT_TRUE(f.format(0.0f, 0).empty());
    EXPECT_TRUE(f.format(1.0f, 0).empty());
}

TEST(TickFormatters, EngFormatterPrefixes) {
    EngFormatter f("Hz");
    EXPECT_EQ(f.format(1234.0f, 0), "1.2 kHz");
    EXPECT_EQ(f.format(0.001f, 0), "1 mHz");
    EXPECT_EQ(f.format(2e6f, 0), "2 MHz");
    EXPECT_EQ(f.format(0.0f, 0), "0 Hz");
}

TEST(TickFormatters, PercentFormatter) {
    PercentFormatter f(100.0f);
    EXPECT_EQ(f.format(50.0f, 0), "50%");
    PercentFormatter frac(1.0f);
    EXPECT_EQ(frac.format(0.25f, 0), "25%");
}

// ═══ Rendering ══════════════════════════════════════════════════════════════

namespace {

struct TickFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit TickFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        axes->style().xAxis.visible = true;
        axes->style().yAxis.visible = true;
        // A data series so autoscale gives a real viewport.
        Series2D s;
        s.points = {{0.0f, 0.0f}, {10.0f, 10.0f}};
        s.color = Color::blue();
        axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
        axes->setViewport({0, 10, 0, 10});
    }
    Image render() { return harness.render(figure); }
};

/// Count dark pixels in a horizontal strip below the axes rect bottom
/// (where outward x tick marks live).
size_t darkInStrip(const Image& img, uint32_t x0, uint32_t x1,
                   uint32_t y0, uint32_t y1) {
    size_t n = 0;
    for (uint32_t y = y0; y < y1; ++y)
        for (uint32_t x = x0; x < x1; ++x) {
            Pixel p = img.get(x, y);
            if (p.r < 180 && p.g < 180 && p.b < 180) ++n;
        }
    return n;
}

} // namespace

TEST(TickRender, MinorTicksAddMarks) {
    // Enabling minor ticks adds extra marks below the axes edge.
    TickFigure off(256);
    auto imgOff = off.render();

    TickFigure on(256);
    on.axes->minorticksOn();
    auto imgOn = on.render();

    // Axes rect bottom ≈ y 235 (default margins). Tick marks sit just
    // below the spine: y 237..243.
    size_t off_ = darkInStrip(imgOff, 30, 230, 237, 243);
    size_t on_  = darkInStrip(imgOn, 30, 230, 237, 243);
    EXPECT_GT(on_, off_) << "minor ticks should add extra tick marks";
}

TEST(TickRender, TickDirectionIn) {
    // direction="in" → ticks drawn inside the axes rect.
    TickFigure tf(256);
    tf.axes->tickParams("both", "in");
    auto img = tf.render();

    // Just inside the bottom edge, above the spine (≈ y 234-235).
    size_t inside = darkInStrip(img, 30, 230, 226, 232);
    EXPECT_GT(inside, 4u) << "direction=in should draw ticks inside axes";
}

TEST(TickRender, TickDirectionOutIsDefault) {
    TickFigure tf(256);
    auto img = tf.render();

    // direction=out (default): no ticks inside, marks below the spine.
    size_t inside = darkInStrip(img, 30, 230, 226, 232);
    size_t below = darkInStrip(img, 30, 230, 237, 243);
    EXPECT_EQ(inside, 0u) << "out ticks should not intrude into the axes";
    EXPECT_GT(below, 4u) << "out ticks should render below the axes";
}

TEST(TickRender, NullLocatorSuppressesTicks) {
    TickFigure tf(256);
    tf.axes->setXLocator(std::make_shared<NullLocator>());
    tf.axes->setXFormatter(std::make_shared<NullFormatter>());
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // No x tick marks below the edge and no x labels.
    size_t below = darkInStrip(img, 30, 230, 237, 243);
    EXPECT_EQ(below, 0u) << "NullLocator should draw no tick marks";
    size_t labels = darkInStrip(img, 30, 230, 244, 256);
    EXPECT_EQ(labels, 0u) << "NullFormatter should draw no labels";
}

TEST(TickRender, FixedLocatorAndFormatter) {
    TickFigure tf(256);
    tf.axes->setXLocator(std::make_shared<FixedLocator>(
        std::vector<float>{5.0f}));
    tf.axes->setXFormatter(std::make_shared<FixedFormatter>(
        std::vector<std::string>{"MID"}));
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // Single tick mark at x=5 (center of [0,10] → canvas x ≈ 128).
    size_t center = darkInStrip(img, 115, 145, 237, 243);
    EXPECT_GT(center, 2u) << "fixed tick at x=5 should render";
    // No tick marks near the edges (fixed locator suppresses others).
    size_t left = darkInStrip(img, 25, 60, 237, 243);
    EXPECT_EQ(left, 0u);
}

TEST(TickRender, MultipleLocatorChangesTicks) {
    TickFigure tf(256);
    tf.axes->setXLocator(std::make_shared<MultipleLocator>(1.0f));
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // Multiples of 1 in [0,10] → 11 ticks, denser than the ~6 default.
    size_t below = darkInStrip(img, 30, 230, 237, 243);
    EXPECT_GT(below, 30u) << "MultipleLocator(1) should draw dense ticks";
}

TEST(TickRender, GridMinorWhich) {
    // grid(which="both") renders minor grid lines (more grid pixels).
    TickFigure major(256);
    major.axes->grid(true, "major");
    auto imgMaj = major.render();

    TickFigure both(256);
    both.axes->grid(true, "both");
    auto imgBoth = both.render();

    auto gridPx = [](const Image& img) {
        return img.countIf([](uint32_t, uint32_t, Pixel p) {
            // grid gray ≈ 176 (major) / 215 (minor)
            return p.r > 140 && p.r < 235 && p.g == p.r && p.b == p.r;
        });
    };
    EXPECT_GT(gridPx(imgBoth), gridPx(imgMaj))
        << "grid(which=both) should add minor grid lines";
}

TEST(TickRender, GridAxisXOnly) {
    TickFigure tf(256);
    tf.axes->grid(true, "major", "x");
    auto img = tf.render();

    // Vertical grid lines exist but no horizontal ones: count grid
    // pixels in a horizontal band mid-axes vs a vertical band.
    // With x-only grid there are vertical lines → dark/gray columns
    // crossing the band. Check that a horizontal strip is not uniformly
    // crossed by a horizontal line: sample row y=120 across x.
    uint32_t grayCols = 0;
    for (uint32_t x = 30; x < 230; ++x) {
        Pixel p = img.get(x, 120);
        if (p.r > 140 && p.r < 235 && std::abs(int(p.r) - int(p.g)) < 5)
            ++grayCols;
    }
    EXPECT_GT(grayCols, 2u) << "x-axis grid should draw vertical lines";
}

TEST(TickRender, TicklabelFormatSci) {
    // ticklabelFormat(style="sci") produces an offset/exponent text at
    // the right end of the x axis.
    TickFigure tf(256);
    tf.axes->ticklabelFormat("x", "sci");
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // Offset text renders below the axis near the right edge.
    size_t corner = darkInStrip(img, 190, 250, 242, 256);
    EXPECT_GT(corner, 2u) << "sci format should render offset text";
}

TEST(TickRender, FuncFormatterLabels) {
    TickFigure tf(256);
    tf.axes->setXLocator(std::make_shared<FixedLocator>(
        std::vector<float>{5.0f}));
    tf.axes->setXFormatter(std::make_shared<FuncFormatter>(
        [](float, int) { return std::string("HELLO"); }));
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    size_t labels = darkInStrip(img, 90, 170, 244, 256);
    EXPECT_GT(labels, 3u) << "FuncFormatter label should render";
}

TEST(TickRender, MinorFormatterLabels) {
    TickFigure tf(256);
    tf.axes->minorticksOn();
    tf.axes->setXMinorFormatter(std::make_shared<FormatStrFormatter>("%.0f"));
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // Minor labels appear below the major label row.
    size_t below = darkInStrip(img, 30, 230, 246, 256);
    EXPECT_GT(below, 5u) << "minor formatter should draw small labels";
}

TEST(TickRender, TicksTop) {
    TickFigure tf(256);
    tf.axes->setXTicksTop(true);
    tf.axes->style().yAxis.visible = false;
    auto img = tf.render();

    // Tick marks now at the top edge (rect top ≈ y 20): above it.
    size_t above = darkInStrip(img, 30, 230, 14, 20);
    EXPECT_GT(above, 4u) << "xTicksTop should draw ticks at the top";
}

TEST(TickRender, AxesConvenienceAccessors) {
    Axes ax;
    ax.setXLocator(std::make_shared<MultipleLocator>(2.0f));
    EXPECT_NE(ax.style().xAxis.ticks.locator, nullptr);
    ax.setYFormatter(std::make_shared<PercentFormatter>(100.0f));
    EXPECT_NE(ax.style().yAxis.ticks.formatter, nullptr);
    ax.minorticksOn();
    EXPECT_TRUE(ax.style().xAxis.ticks.minor);
    EXPECT_TRUE(ax.style().yAxis.ticks.minor);
    ax.minorticksOff();
    EXPECT_FALSE(ax.style().xAxis.ticks.minor);
    ax.tickParams("x", "in", 8.0f);
    EXPECT_EQ(ax.style().xAxis.ticks.direction, "in");
    EXPECT_FLOAT_EQ(ax.style().xAxis.ticks.majorSize, 8.0f);
    ax.grid(true, "both", "y");
    EXPECT_TRUE(ax.style().yAxis.grid);
    EXPECT_EQ(ax.style().yAxis.gridWhich, "both");
    EXPECT_FALSE(ax.style().xAxis.grid);
}
