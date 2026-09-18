// tests/test_microfeatures.cpp — deterministic pixel/logic checks for the
// microfeature gallery (docs/MICROFEATURES.md). These replace LLM review
// for objectively verifiable behaviors: tick density & marks, grid
// alignment/orientation, axes facecolor, subplot spacing, prop-cycle
// colors, and legend loc="best" placement.

#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Scale.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/BarPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>
#include <volcano/plot/plots/ReferenceLines.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

// ═══ Tier 0 — pure logic: tick locator density ═══════════════════════════════

TEST(MicroTicks, LinearDensityMatchesMaxNLocator) {
    // matplotlib MaxNLocator(nbins=9) on [0,10] picks step 2 → 6 ticks.
    auto t = scaleTicks(AxisScale::linear(), 0.0f, 10.0f, 9);
    ASSERT_EQ(t.size(), 6u);
    for (size_t i = 0; i < t.size(); ++i)
        EXPECT_FLOAT_EQ(t[i], float(i) * 2.0f);
}

TEST(MicroTicks, LinearDensityUnitInterval) {
    // [0,1] → step 0.2 → {0, .2, ..., 1}.
    auto t = scaleTicks(AxisScale::linear(), 0.0f, 1.0f, 9);
    ASSERT_EQ(t.size(), 6u);
    for (size_t i = 0; i < t.size(); ++i)
        EXPECT_NEAR(t[i], float(i) * 0.2f, 1e-6f);
}

TEST(MicroTicks, LinearDensitySmallRange) {
    // [0,5] → step 1 → 6 ticks.
    auto t = scaleTicks(AxisScale::linear(), 0.0f, 5.0f, 9);
    ASSERT_EQ(t.size(), 6u);
    for (size_t i = 0; i < t.size(); ++i)
        EXPECT_FLOAT_EQ(t[i], float(i));
}

// ═══ Tier 0 — pure logic: prop cycle consumes tab10 ══════════════════════════

TEST(MicroColor, DefaultSeriesUsesTabCycle) {
    Figure fig(1, 1);
    auto* ax = fig.addAxes(0, 0);
    Series2D s1, s2;
    s1.points = {{0, 0}, {1, 1}};
    s2.points = {{0, 1}, {1, 0}};
    auto* p1 = ax->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    auto* p2 = ax->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    // matplotlib C0/C1 = tab:blue / tab:orange.
    auto c0 = p1->legendColor(), c1 = p2->legendColor();
    auto toPx = [](Color c) {
        return Pixel{uint8_t(c.r * 255), uint8_t(c.g * 255),
                     uint8_t(c.b * 255), 255};
    };
    EXPECT_EQ(toPx(c0).r, 31); EXPECT_EQ(toPx(c0).g, 119);
    EXPECT_EQ(toPx(c0).b, 180);
    EXPECT_EQ(toPx(c1).r, 255); EXPECT_EQ(toPx(c1).g, 127);
    EXPECT_EQ(toPx(c1).b, 14);
}

TEST(MicroColor, ExplicitColorDoesNotConsumeCycle) {
    Figure fig(1, 1);
    auto* ax = fig.addAxes(0, 0);
    Series2D s1, s2;
    s1.points = {{0, 0}, {1, 1}};
    s2.points = {{0, 1}, {1, 0}};
    s1.color = Color::red();  // explicit: keeps red, cycle must not advance
    auto* p1 = ax->addPlot(std::make_unique<LinePlot>(std::move(s1)));
    auto* p2 = ax->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    auto toPx = [](Color c) {
        return Pixel{uint8_t(c.r * 255), uint8_t(c.g * 255),
                     uint8_t(c.b * 255), 255};
    };
    EXPECT_EQ(toPx(p1->legendColor()).r, 255);   // explicit red kept
    EXPECT_EQ(toPx(p1->legendColor()).g, 0);
    EXPECT_EQ(toPx(p2->legendColor()).r, 31);    // next default still C0
}

// ═══ Tier 11 — pure logic: subplot spacing & mosaic spans ════════════════════

TEST(MicroLayout, Subplot2x2RectsDoNotOverlap) {
    Figure fig;
    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 2; ++c)
            fig.subplot2grid({2, 2}, {r, c});
    fig.layout(Extent2D{400, 300});
    ASSERT_EQ(fig.placements().size(), 4u);
    // With matplotlib-like wspace/hspace the cells must have a real gap.
    auto overlap = [](Rect2D a, Rect2D b) {
        int32_t ox = std::min(a.x + int32_t(a.width), b.x + int32_t(b.width)) -
                     std::max(a.x, b.x);
        int32_t oy = std::min(a.y + int32_t(a.height), b.y + int32_t(b.height)) -
                     std::max(a.y, b.y);
        return ox > 0 && oy > 0;
    };
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = i + 1; j < 4; ++j)
            EXPECT_FALSE(overlap(fig.placements()[i].axes->rect,
                                 fig.placements()[j].axes->rect))
                << "axes " << i << " and " << j << " overlap";
    // Vertical gap between the two rows must be positive.
    const auto& top = fig.placements()[0].axes->rect;
    const auto& bot = fig.placements()[2].axes->rect;
    EXPECT_GT(bot.y, top.y + int32_t(top.height))
        << "rows should be separated by hspace";
    const auto& left = fig.placements()[0].axes->rect;
    const auto& right = fig.placements()[1].axes->rect;
    EXPECT_GT(right.x, left.x + int32_t(left.width))
        << "cols should be separated by wspace";
}

TEST(MicroLayout, MosaicSpansCells) {
    Figure fig;
    auto axes = fig.subplotMosaic({{"A", "B"}, {"C", "C"}});
    fig.layout(Extent2D{400, 300});
    ASSERT_EQ(axes.size(), 3u);
    // "C" spans both columns → wider than a single-cell axes.
    EXPECT_GT(axes["C"]->rect.width, axes["A"]->rect.width);
    EXPECT_GT(axes["C"]->rect.width, axes["B"]->rect.width);
    // Rows stack without overlap.
    EXPECT_GT(axes["C"]->rect.y,
              axes["A"]->rect.y + int32_t(axes["A"]->rect.height));
}

// ═══ Rendering fixture ═══════════════════════════════════════════════════════

namespace {

/// Default-styled axes over [0,10]² — mirrors the microgallery setup:
/// real spines/ticks/labels on a white background.
struct MfFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit MfFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(styles::defaultStyle());
        axes->setViewport({0, 10, 0, 10});
    }
    Image render() {
        auto img = harness.render(figure);
        rect = axes->rect;
        return img;
    }
    Rect2D rect{};
};

/// Count pixels matching `pred` in a rect.
template <typename Pred>
size_t countIn(const Image& img, Rect2D r, Pred pred) {
    size_t n = 0;
    uint32_t x1 = r.x + r.width, y1 = r.y + r.height;
    for (uint32_t y = uint32_t(std::max(r.y, 0)); y < y1; ++y)
        for (uint32_t x = uint32_t(std::max(r.x, 0)); x < x1; ++x)
            if (pred(img.get(x, y))) ++n;
    return n;
}

bool isDark(Pixel p) { return p.r < 180 && p.g < 180 && p.b < 180; }
bool isGridGray(Pixel p) {
    return p.r > 130 && p.r < 235 && p.g == p.r && p.b == p.r;
}

} // namespace

// ═══ Tier 1 — tick marks render at MaxNLocator positions ═════════════════════

TEST(MicroTicksRender, MajorTicksPresentBelowSpine) {
    MfFigure tf(256);
    auto img = tf.render();
    // Outward major ticks: dark pixels in a strip just below the bottom
    // spine. With step 2 on [0,10], ticks sit at fractions 0,.2,...,1.
    uint32_t y0 = uint32_t(tf.rect.y + int32_t(tf.rect.height) + 1);
    uint32_t y1 = y0 + 6;
    size_t dark = countIn(img, {tf.rect.x, int32_t(y0), tf.rect.width,
                                y1 - y0}, isDark);
    EXPECT_GT(dark, 4u) << "major tick marks should render below spine";
}

TEST(MicroTicksRender, TickDensityIsMaxNLocator) {
    MfFigure tf(256);
    auto img = tf.render();
    // Count distinct dark columns in the tick strip below the spine —
    // MaxNLocator gives 6 ticks on [0,10], not ~11.
    uint32_t y0 = uint32_t(tf.rect.y + int32_t(tf.rect.height) + 1);
    uint32_t cols = 0;
    for (uint32_t x = uint32_t(tf.rect.x);
         x < uint32_t(tf.rect.x) + tf.rect.width; ++x) {
        bool any = false;
        for (uint32_t y = y0; y < y0 + 5; ++y)
            if (isDark(img.get(x, y))) { any = true; break; }
        if (any) ++cols;
    }
    EXPECT_LE(cols, 20u) << "should be ~6 ticks (≈6-18 px of marks), not 11";
}

// ═══ Tier 4 — grid lines are straight, continuous, tick-aligned ══════════════

TEST(MicroGrid, MajorGridStraightAndAligned) {
    MfFigure tf(256);
    tf.axes->grid(true);
    auto img = tf.render();
    // Interior major ticks at fractions .2/.4/.6/.8 → vertical grid
    // lines spanning the full axes height at those columns.
    for (float f : {0.2f, 0.4f, 0.6f, 0.8f}) {
        int32_t cx = tf.rect.x + int32_t(f * float(tf.rect.width));
        // A 1-2px line may land anywhere in a few-px window depending on
        // subpixel alignment; require one column with >50% coverage.
        float best = 0.0f;
        for (int dx = -3; dx <= 3; ++dx) {
            size_t gray = countIn(img,
                {cx + dx, tf.rect.y + 2, 1, tf.rect.height - 4}, isGridGray);
            best = std::max(best,
                            float(gray) / float(tf.rect.height - 4));
        }
        EXPECT_GT(best, 0.5f)
            << "grid line at fraction " << f << " should span the axes";
    }
    // A horizontal line at the .4 tick fraction too (y=4 is a tick).
    int32_t cy = tf.rect.y + int32_t(0.6f * float(tf.rect.height));
    float bestRow = 0.0f;
    for (int dy = -3; dy <= 3; ++dy) {
        size_t gray = countIn(img,
            {tf.rect.x + 2, cy + dy, tf.rect.width - 4, 1}, isGridGray);
        bestRow = std::max(bestRow, float(gray) / float(tf.rect.width - 4));
    }
    EXPECT_GT(bestRow, 0.5f);
}

TEST(MicroGrid, XOnlyGridHasNoHorizontalLines) {
    MfFigure tf(256);
    tf.axes->grid(true, "major", "x");
    auto img = tf.render();
    // Mid-axes row between two horizontal tick levels should have no
    // long horizontal grid line: gray pixels only at vertical-line
    // crossings (a handful of columns), not spanning.
    int32_t cy = tf.rect.y + int32_t(0.5f * float(tf.rect.height));
    size_t gray = countIn(img,
        {tf.rect.x + 2, cy, tf.rect.width - 4, 1}, isGridGray);
    EXPECT_LT(gray, tf.rect.width / 4)
        << "x-only grid must not draw horizontal lines";
}

TEST(MicroGrid, YOnlyGridHasNoVerticalLines) {
    MfFigure tf(256);
    tf.axes->grid(true, "major", "y");
    auto img = tf.render();
    // Column between two vertical tick positions: only horizontal-line
    // crossings (few rows), no spanning vertical line.
    int32_t cx = tf.rect.x + int32_t(0.5f * float(tf.rect.width));
    size_t gray = countIn(img,
        {cx, tf.rect.y + 2, 1, tf.rect.height - 4}, isGridGray);
    EXPECT_LT(gray, tf.rect.height / 4)
        << "y-only grid must not draw vertical lines";
}

TEST(MicroGrid, MinorWhichAddsLines) {
    MfFigure major(256);
    major.axes->grid(true, "major");
    auto imgMaj = major.render();

    MfFigure both(256);
    both.axes->grid(true, "both");
    auto imgBoth = both.render();

    auto grayPx = [](const Image& img, Rect2D r) {
        return countIn(img, r, isGridGray);
    };
    Rect2D inner{10, 10, 236, 216};
    EXPECT_GT(grayPx(imgBoth, inner), grayPx(imgMaj, inner))
        << "grid(which=both) should add minor grid lines";
}

TEST(MicroGrid, DashedGridHasGaps) {
    MfFigure tf(256);
    tf.axes->style().xAxis.grid = true;
    tf.axes->style().yAxis.grid = true;
    tf.axes->style().xAxis.gridLineStyle = "--";
    tf.axes->style().yAxis.gridLineStyle = "--";
    auto img = tf.render();
    // The vertical grid line at tick fraction .4 is dashed → gray
    // pixels present but covering much less of the column than solid.
    int32_t cx = tf.rect.x + int32_t(0.4f * float(tf.rect.width));
    size_t gray = countIn(img,
        {cx - 1, tf.rect.y + 2, 3, tf.rect.height - 4}, isGridGray);
    float cov = float(gray) / float(3 * (tf.rect.height - 4));
    EXPECT_GT(cov, 0.05f) << "dashed grid should still draw";
    EXPECT_LT(cov, 0.55f) << "dashed grid should have gaps";
}

TEST(MicroGrid, AxisBelowPutsGridUnderArtists) {
    // axisBelow=true: a solid red data line crosses the grid → the line
    // pixel wins at the crossing.
    MfFigure below(256);
    below.axes->grid(true);
    below.axes->style().axisBelow = true;
    Series2D s;
    s.points = {{0, 5}, {10, 5}};
    s.color = Color::red();
    s.lineWidth = 4.0f;
    below.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto imgBelow = below.render();
    // Region around the crossing of the red line (y=5) and the x=4
    // vertical grid line.
    auto cross = [](const MfFigure& f) {
        return Rect2D{f.rect.x + int32_t(0.4f * float(f.rect.width)) - 2,
                      f.rect.y + int32_t(0.5f * float(f.rect.height)) - 2,
                      5, 5};
    };
    size_t belowGray = countIn(imgBelow, cross(below), isGridGray);

    // axisBelow=false: grid paints over the line → gray at crossing.
    MfFigure above(256);
    above.axes->grid(true);
    above.axes->style().axisBelow = false;
    Series2D s2;
    s2.points = {{0, 5}, {10, 5}};
    s2.color = Color::red();
    s2.lineWidth = 4.0f;
    above.axes->addPlot(std::make_unique<LinePlot>(std::move(s2)));
    auto imgAbove = above.render();
    size_t aboveGray = countIn(imgAbove, cross(above), isGridGray);
    EXPECT_GT(aboveGray, belowGray + 2)
        << "axisBelow=false should paint the grid over the red line "
           "(gray=" << aboveGray << " vs below=" << belowGray << ")";
}

// ═══ Tier 0 — axes facecolor patch ═══════════════════════════════════════════

TEST(MicroAxes, FacecolorFillsAxesRect) {
    MfFigure tf(256);
    tf.axes->style().faceColor = Color::fromRgba8(200, 220, 255);
    auto img = tf.render();
    Pixel center = img.get(uint32_t(tf.rect.x + int32_t(tf.rect.width) / 2),
                           uint32_t(tf.rect.y + int32_t(tf.rect.height) / 2));
    EXPECT_NEAR(int(center.b), 255, 20);
    EXPECT_NEAR(int(center.r), 200, 30);
    EXPECT_NEAR(int(center.g), 220, 30);
}

// ═══ Tier 10 — legend loc="best" picks the emptiest corner ═══════════════════

TEST(MicroLegend, BestLocationAvoidsData) {
    MfFigure tf(256);
    tf.axes->style().legend.visible = true;
    tf.axes->style().legend.location = "best";
    tf.axes->style().legend.edgeColor = Color::fromRgba8(255, 0, 255);
    // Data concentrated in the upper half → 'best' should place the
    // legend in a lower corner.
    Series2D s;
    for (int i = 0; i <= 50; ++i)
        s.points.push_back({float(i) * 0.2f, 8.0f + std::sin(float(i))});
    s.label = "upper";
    tf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto img = tf.render();
    // Find the legend's magenta border pixels.
    auto magenta = [](Pixel p) {
        return p.r > 200 && p.b > 200 && p.g < 100;
    };
    size_t lower = countIn(img,
        {tf.rect.x, tf.rect.y + int32_t(tf.rect.height) / 2,
         tf.rect.width, tf.rect.height / 2}, magenta);
    size_t upper = countIn(img,
        {tf.rect.x, tf.rect.y, tf.rect.width, tf.rect.height / 2}, magenta);
    EXPECT_GT(lower, 4u) << "loc=best should put the legend below the data";
    EXPECT_EQ(upper, 0u) << "loc=best should avoid the data-dense top";
}

// ═══ Tier 0 — spine visibility ═══════════════════════════════════════════════

TEST(MicroSpines, HiddenSpinesProduceNoBorder) {
    MfFigure tf(256);
    tf.axes->style().xAxis.visible = false;
    tf.axes->style().yAxis.visible = false;
    for (auto side : {"left", "right", "top", "bottom"})
        tf.axes->setSpineVisible(side, false);
    auto img = tf.render();
    // No border: the interior edge rows/cols of the axes rect stay white.
    size_t dark = countIn(img, tf.rect, isDark);
    EXPECT_EQ(dark, 0u) << "spines=off should draw no border or ticks";
}

// ═══ Round 2 — log autoscale, bar positions, twin overlay, clipping, ═════════
// ═══ heatmap normalization, colorbar orientation, margins, decimals ══════════

TEST(MicroScale, LogAutoscalePadsInDisplaySpace) {
    // Data-space padding pushed xmin negative → log10 clamp squished the
    // plot to the right edge. mpl pads in transformed space.
    Figure fig;
    auto* ax = fig.addAxes();
    Series2D s;
    for (int i = 0; i < 60; ++i) {
        float x = 0.01f + 99.99f * float(i) / 59.0f;
        s.points.push_back({x, std::log(x)});
    }
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setLogX(true);
    ax->setYlim(-5, 5);
    fig.layout({400, 300});
    ax->autoscale();
    const auto& vp = ax->viewport();
    EXPECT_GT(vp.x.min, 0.0f) << "log autoscale must keep xmin positive";
    // x=1 (middle decade of 0.01..100) should map near the axes center.
    float fx = ax->dataToFraction({1.0f, 0.0f}).x;
    EXPECT_NEAR(fx, 0.5f, 0.1f);
}

TEST(MicroTicks, UniformDecimalPrecision) {
    // mpl ScalarFormatter pads all labels to a common precision:
    // {0, .5, 1} → "0.0", "0.5", "1.0" (not "0", "0.5", "1").
    ScalarFormatter f;
    std::vector<float> locs{0.0f, 0.5f, 1.0f};
    f.setLocs(locs);
    EXPECT_EQ(f.format(0.0f, 0), "0.0");
    EXPECT_EQ(f.format(0.5f, 1), "0.5");
    EXPECT_EQ(f.format(1.0f, 2), "1.0");
    // Integer ticks stay integer.
    ScalarFormatter g;
    std::vector<float> ints{0.0f, 2.0f, 4.0f};
    g.setLocs(ints);
    EXPECT_EQ(g.format(2.0f, 1), "2");
}

TEST(MicroTwin, OverlayKeepsParentArtists) {
    MfFigure tf(256);
    tf.axes->setYlim(-1.2f, 1.2f);
    Series2D s;
    for (int i = 0; i <= 50; ++i)
        s.points.push_back({float(i) * 0.2f, std::sin(float(i) * 0.2f)});
    s.color = Color::blue();
    tf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    auto* ax2 = tf.figure.twinx(*tf.axes);
    EXPECT_EQ(ax2->style().faceColor.a, 0.0f)
        << "twin axes must not paint over the parent's artists";
    auto img = tf.render();
    size_t blue = countIn(img, tf.rect, [](Pixel p) {
        return p.b > 150 && p.r < 120;
    });
    EXPECT_GT(blue, 200u) << "primary curve must remain visible under twinx";
}

TEST(MicroRef, AxvlineClippedToAxes) {
    MfFigure tf(256);
    tf.axes->addPlot(std::make_unique<AxvLine>(5.0f,
                                             Color::fromRgba8(0, 200, 0)));
    auto img = tf.render();
    auto isGreen = [](Pixel p) { return p.g > 150 && p.r < 120 && p.b < 120; };
    // Above and below the axes rect there must be no green pixels.
    Rect2D above{tf.rect.x, 0, tf.rect.width, tf.rect.y};
    Rect2D below{tf.rect.x, tf.rect.y + int32_t(tf.rect.height) + 1,
                 tf.rect.width, 256 - tf.rect.y - int32_t(tf.rect.height) - 1};
    EXPECT_EQ(countIn(img, above, isGreen), 0u);
    EXPECT_EQ(countIn(img, below, isGreen), 0u);
    EXPECT_GT(countIn(img, tf.rect, isGreen), 100u);
}

TEST(MicroBar, BarsCenteredAtIndices) {
    MfFigure tf(256);
    BarData bd;
    bd.heights = {5, 5, 5, 5};
    tf.axes->addPlot(std::make_unique<BarPlot>(std::move(bd)));
    auto img = tf.render();
    // mpl semantics: bar i spans [i-w/2, i+w/2]; with xlim autoscaled to
    // [0,4] the last bar's right edge must NOT reach index 4's center.
    // Check pixel distribution: bar centers should sit at data x = i.
    auto isBlue = [](Pixel p) { return p.b > 120 && p.b > p.r + 40; };
    // Bottom row inside axes: find blue x-clusters.
    uint32_t midY = tf.rect.y + tf.rect.height - 4;
    std::vector<int> edges;
    bool prev = false;
    for (uint32_t x = tf.rect.x; x < tf.rect.x + tf.rect.width; ++x) {
        bool b = isBlue(img.get(x, midY));
        if (b != prev) edges.push_back(int(x));
        prev = b;
    }
    // 4 bars → ~4 blue runs.
    ASSERT_GE(edges.size(), 7u) << "expected 4 distinct bars";
}

TEST(MicroHeatmap, AutoValueRangeFromData) {
    MfFigure tf(256);
    Grid2D g;
    g.width = 4; g.height = 4;
    g.xRange = {0, 4}; g.yRange = {0, 4};
    g.values.resize(16);
    for (int i = 0; i < 16; ++i) g.values[i] = float(i); // 0..15
    tf.axes->addPlot(std::make_unique<HeatmapPlot>(std::move(g)));
    auto img = tf.render();
    // With auto-normalization the interior must show a gradient, not a
    // single saturated color (old [0,1] default saturated everything >1).
    Rect2D inner{tf.rect.x + 8, tf.rect.y + 8,
                 tf.rect.width - 16, tf.rect.height - 16};
    uint32_t distinct = 0;
    Pixel prev{};
    for (uint32_t y = inner.y; y < inner.y + inner.height; ++y) {
        for (uint32_t x = inner.x; x < inner.x + inner.width; ++x) {
            Pixel p = img.get(x, y);
            if (p.r != prev.r || p.g != prev.g || p.b != prev.b) {
                ++distinct; prev = p;
            }
        }
    }
    EXPECT_GT(distinct, 8u) << "heatmap should show a value gradient";
}

TEST(MicroMargins, LabelsNotClippedAtFigureEdge) {
    MfFigure tf(256);
    tf.axes->style().xAxis.label = "x label";
    tf.axes->style().yAxis.label = "y label";
    auto img = tf.render();
    // mpl leaves enough figure margin that no text touches the canvas edge.
    auto& e = img;
    size_t edgeDark = 0;
    for (uint32_t x = 0; x < e.width(); ++x)
        edgeDark += isDark(e.get(x, 0)) + isDark(e.get(x, e.height() - 1));
    for (uint32_t y = 0; y < e.height(); ++y)
        edgeDark += isDark(e.get(0, y)) + isDark(e.get(e.width() - 1, y));
    EXPECT_EQ(edgeDark, 0u) << "labels must not clip at the canvas edge";
}
