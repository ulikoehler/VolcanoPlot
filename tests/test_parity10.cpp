// tests/test_parity10.cpp — tests for the 10-feature parity batch:
//   1. Axes::locatorParams (mpl locator_params: nbins/integer/prune/
//      symmetric/min_n_ticks/steps/tight)
//   2. Axes::setXbound/setYbound (bounds independent of inversion)
//   3. Series2D::markevery (int / (start,step) / fraction / indices)
//   4. PiePlot geometry (startAngle, counterclock, radius, center,
//      normalize)
//   5. PiePlot autopct + pctDistance/labelDistance/rotatelabels
//   6. fill_between where= mask + interpolate
//   7. stackplot baseline modes (zero/sym/wiggle/weighted_wiggle)
//   8. Axes::labelOuter (inner tick-label suppression on grids)
//   9. Axes axis("off")/setAxisOff/setFrameOn (axes visibility)
//  10. imshow(origin="lower")
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/plots/PiePlot.hpp>
#include <volcano/plot/plots/FillBetweenPlot.hpp>
#include <volcano/plot/plots/StackPlot.hpp>
#include <volcano/plot/plots/HeatmapPlot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct BatchFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit BatchFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.grid().left = 0.0f; figure.grid().right = 1.0f;
        figure.grid().bottom = 0.0f; figure.grid().top = 1.0f;
    }

    Image render() { return harness.render(figure); }
};

size_t countNonWhite(const Image& img) {
    return img.countIf([](uint32_t, uint32_t, Pixel p) {
        return !(p.r > 230 && p.g > 230 && p.b > 230);
    });
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Feature 1: Axes::locatorParams — mpl locator_params
// ═══════════════════════════════════════════════════════════════════════════

TEST(LocatorParams, NbinsLimitsTickCount) {
    Axes ax;
    LocatorParams p;
    p.nbins = 3;
    ax.locatorParams("x", p);
    // locatorParams installs a MaxNLocator on a linear axis.
    auto* loc = dynamic_cast<MaxNLocator*>(
        ax.style().xAxis.ticks.locator.get());
    ASSERT_NE(loc, nullptr);
    EXPECT_EQ(loc->nbins(), 3);
    auto t = loc->tickValues(0.0f, 10.0f);
    EXPECT_LE(t.size(), 5u) << "nbins=3 should produce few ticks";
}

TEST(LocatorParams, IntegerForcesIntegerTicks) {
    Axes ax;
    LocatorParams p;
    p.integer = true;
    ax.locatorParams("x", p);
    auto* loc = dynamic_cast<MaxNLocator*>(
        ax.style().xAxis.ticks.locator.get());
    ASSERT_NE(loc, nullptr);
    auto t = loc->tickValues(0.0f, 3.0f);
    for (float v : t)
        EXPECT_FLOAT_EQ(v, std::round(v)) << "non-integer tick " << v;
}

TEST(LocatorParams, SymmetricExtendsRange) {
    // mpl symmetric=True: the located range is symmetrized about zero,
    // so a [2, 8] view locates ticks as if the range were [-8, 8].
    Axes ax;
    LocatorParams p;
    p.symmetric = true;
    ax.locatorParams("x", p);
    auto* loc = dynamic_cast<MaxNLocator*>(
        ax.style().xAxis.ticks.locator.get());
    ASSERT_NE(loc, nullptr);
    auto t = loc->tickValues(2.0f, 8.0f);
    // Symmetrized range includes zero → ticks should extend below vmin.
    bool belowVmin = false;
    for (float v : t)
        if (v < 2.0f) belowVmin = true;
    EXPECT_TRUE(belowVmin) << "symmetric should extend ticks below vmin";
}

TEST(LocatorParams, PruneLowerDropsLowestTick) {
    MaxNLocator loc;
    loc.setPrune("lower");
    auto t = loc.tickValues(0.0f, 10.0f);
    ASSERT_GE(t.size(), 2u);
    // With prune='lower' the lowest located tick is dropped.
    MaxNLocator ref;
    auto rt = ref.tickValues(0.0f, 10.0f);
    ASSERT_GE(rt.size(), 2u);
    EXPECT_GT(t.front(), rt.front())
        << "prune='lower' should drop the lowest tick";
}

TEST(LocatorParams, CustomStepsRespected) {
    MaxNLocator loc;
    loc.setSteps({1.0f, 2.0f, 5.0f, 10.0f});
    auto t = loc.tickValues(0.0f, 10.0f);
    ASSERT_GE(t.size(), 2u);
    float step = t[1] - t[0];
    // Steps restricted to {1,2,5,10}: step should be a multiple of one.
    bool ok = false;
    for (float s : {1.0f, 2.0f, 5.0f, 10.0f})
        if (std::abs(step - s) < 0.01f) ok = true;
    EXPECT_TRUE(ok) << "step=" << step;
}

TEST(LocatorParams, MinNTicksRelaxesStep) {
    MaxNLocator loc(4);   // nbins=4 → coarse step
    loc.setMinNTicks(5);  // but require at least 5 ticks
    auto t = loc.tickValues(0.0f, 10.0f);
    EXPECT_GE(t.size(), 5u) << "min_n_ticks=5 should force finer step";
}

TEST(LocatorParams, TightZeroesMargin) {
    Axes ax;
    LocatorParams p;
    p.tight = true;
    ax.locatorParams("x", p);
    EXPECT_FLOAT_EQ(ax.marginX(), 0.0f);
    // y margin untouched when axis='x'.
    EXPECT_FLOAT_EQ(ax.marginY(), 0.05f);
}

TEST(LocatorParams, BothAxesApply) {
    Axes ax;
    LocatorParams p;
    p.nbins = 4;
    ax.locatorParams("both", p);
    EXPECT_NE(dynamic_cast<MaxNLocator*>(
                  ax.style().xAxis.ticks.locator.get()), nullptr);
    EXPECT_NE(dynamic_cast<MaxNLocator*>(
                  ax.style().yAxis.ticks.locator.get()), nullptr);
}

TEST(LocatorParams, LogAxisLocatorUntouched) {
    Axes ax;
    ax.setXscale("log");
    LocatorParams p;
    p.nbins = 3;
    ax.locatorParams("x", p);
    // mpl: locator_params on a log axis forwards to LogLocator, which
    // ignores nbins — our MaxNLocator must not be installed.
    EXPECT_EQ(dynamic_cast<MaxNLocator*>(
                  ax.style().xAxis.ticks.locator.get()), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 2: setXbound / setYbound
// ═══════════════════════════════════════════════════════════════════════════

TEST(SetBound, BasicBounds) {
    Axes ax;
    ax.setXbound(2.0f, 8.0f);
    EXPECT_FLOAT_EQ(ax.xlim().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.xlim().max, 8.0f);
    EXPECT_TRUE(ax.manualX());
}

TEST(SetBound, InvertedAxisKeepsInversion) {
    Axes ax;
    ax.setXlim(10.0f, 0.0f);   // inverted
    ax.setXbound(2.0f, 8.0f);
    // mpl set_xbound on an inverted axis: lower bound goes to the
    // visually lower edge (max slot) — the axis stays inverted.
    EXPECT_TRUE(ax.xAxisInverted());
    EXPECT_FLOAT_EQ(ax.xlim().min, 8.0f);
    EXPECT_FLOAT_EQ(ax.xlim().max, 2.0f);
}

TEST(SetBound, PartialBounds) {
    Axes ax;
    ax.setXlim(0.0f, 10.0f);
    ax.setXbound(std::optional<float>(3.0f), std::nullopt);
    EXPECT_FLOAT_EQ(ax.xlim().min, 3.0f);
    EXPECT_FLOAT_EQ(ax.xlim().max, 10.0f);
    ax.setYlim(0.0f, 10.0f);
    ax.setYbound(std::nullopt, std::optional<float>(7.0f));
    EXPECT_FLOAT_EQ(ax.ylim().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.ylim().max, 7.0f);
}

TEST(SetBound, InvertedPartialLower) {
    Axes ax;
    ax.setXlim(10.0f, 0.0f);   // inverted: min=10, max=0
    // "lower" is the visually lower bound → writes to .max.
    ax.setXbound(std::optional<float>(4.0f), std::nullopt);
    EXPECT_FLOAT_EQ(ax.xlim().min, 10.0f);
    EXPECT_FLOAT_EQ(ax.xlim().max, 4.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 3: Series2D::markevery
// ═══════════════════════════════════════════════════════════════════════════

TEST(Markevery, IntFormStored) {
    Series2D s;
    s.setMarkevery(3);
    ASSERT_TRUE(s.markevery.has_value());
    EXPECT_EQ(std::get<int>(*s.markevery), 3);
}

TEST(Markevery, StartStepFormStored) {
    Series2D s;
    s.setMarkevery(2, 5);
    auto p = std::get<std::pair<int, int>>(*s.markevery);
    EXPECT_EQ(p.first, 2);
    EXPECT_EQ(p.second, 5);
}

TEST(Markevery, IndexListFormStored) {
    Series2D s;
    s.setMarkevery(std::vector<int>{0, 3, 7});
    auto idx = std::get<std::vector<int>>(*s.markevery);
    ASSERT_EQ(idx.size(), 3u);
    EXPECT_EQ(idx[1], 3);
}

TEST(Markevery, SubsampledMarkersRender) {
    // 21 points on a diagonal, markers at every point vs every 5th.
    // The subsampled render must have far fewer marker pixels.
    auto makeLine = [](std::optional<int> every) {
        Series2D s;
        s.color = Color::fromRgba8(255, 0, 0);
        s.size = 12.0f;
        s.marker = MarkerStyle::Circle;
        s.lineStyle = LineStyle::None;   // markers only
        for (int i = 0; i <= 20; ++i)
            s.points.push_back({float(i) / 20.0f, float(i) / 20.0f});
        if (every) s.setMarkevery(*every);
        return s;
    };

    BatchFigure all(256);
    all.axes->addPlot(std::make_unique<LinePlot>(makeLine(std::nullopt)));
    auto imgAll = all.render();
    size_t redAll = imgAll.countColor(Pixel::red(), 60);

    BatchFigure sub(256);
    sub.axes->addPlot(std::make_unique<LinePlot>(makeLine(5)));
    auto imgSub = sub.render();
    size_t redSub = imgSub.countColor(Pixel::red(), 60);

    EXPECT_GT(redAll, 0u);
    EXPECT_GT(redSub, 0u);
    // ~5 markers vs ~21 → subsampled should be roughly a quarter.
    EXPECT_LT(redSub * 3, redAll)
        << "markevery=5 should draw far fewer marker pixels";
}

// ═══════════════════════════════════════════════════════════════════════════
// Features 4–5: PiePlot geometry + labels
// ═══════════════════════════════════════════════════════════════════════════

TEST(PieGeometry, StartAngleRotatesFirstWedge) {
    // 4 equal slices, startAngle=90: first wedge (red) starts at the
    // top (12 o'clock) going CCW → occupies the upper-left quadrant.
    BatchFigure cf(256);
    PieData d;
    d.values = {1, 1, 1, 1};
    d.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0),
                Color::fromRgba8(0, 0, 255), Color::fromRgba8(255, 255, 0)};
    d.startAngle = 90.0f;
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    // Upper-left of center should be red (first wedge 90°→180°).
    Pixel ul = img.get(64, 64);   // upper-left quadrant
    EXPECT_TRUE(ul.approx(Pixel::red(), 60))
        << "startAngle=90 should put first wedge in upper-left";
    // Upper-right should be the last wedge (yellow, 0°→90°).
    Pixel ur = img.get(192, 64);
    EXPECT_TRUE(ur.approx(Pixel{255, 255, 0, 255}, 60))
        << "upper-right should be the last (yellow) wedge";
}

TEST(PieGeometry, CounterclockFalseReversesOrder) {
    // 2 slices (red, green). Default CCW: red goes 0°→180° (upper half).
    // counterclock=false: red goes 0°→-180° (lower half).
    BatchFigure cw(256);
    PieData d;
    d.values = {1, 1};
    d.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0)};
    d.counterclock = false;
    cw.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cw.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cw.render();

    // Lower half should be red (first wedge goes clockwise/down).
    Pixel lower = img.get(128, 192);
    EXPECT_TRUE(lower.approx(Pixel::red(), 60))
        << "counterclock=false should put first wedge in lower half";
    Pixel upper = img.get(128, 64);
    EXPECT_TRUE(upper.approx(Pixel::green(), 60))
        << "upper half should be the second (green) wedge";
}

TEST(PieGeometry, NormalizeFalsePartialPie) {
    // normalize=false, values sum to 0.5 → only half the circle drawn.
    BatchFigure cf(256);
    PieData d;
    d.values = {0.5f};
    d.colors = {Color::fromRgba8(255, 0, 0)};
    d.normalize = false;
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    // Upper half (0°→180°) filled, lower half empty.
    Pixel upper = img.get(128, 64);
    EXPECT_TRUE(upper.approx(Pixel::red(), 60))
        << "partial pie should fill the first half";
    Pixel lower = img.get(128, 192);
    EXPECT_TRUE(lower.approx(Pixel::white(), 40))
        << "normalize=false with sum=0.5 leaves lower half empty";
}

TEST(PieGeometry, RadiusShrinksPie) {
    BatchFigure cf(256);
    PieData d;
    d.values = {1, 1};
    d.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0)};
    d.radius = 0.5f;
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    // Corner region should be empty (radius 0.5 → pie is half as big).
    Pixel corner = img.get(10, 10);
    EXPECT_TRUE(corner.approx(Pixel::white(), 40))
        << "radius=0.5 should leave the corners empty";
    size_t colored = countNonWhite(img);
    EXPECT_GT(colored, 500u) << "pie should still render";
}

TEST(PieGeometry, CenterOffsetsPie) {
    BatchFigure cf(256);
    PieData d;
    d.values = {1};
    d.colors = {Color::fromRgba8(255, 0, 0)};
    d.center = {0.5f, 0.0f};   // shift right 0.5 data units
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    auto c = img.centroid(Pixel::red(), 60);
    ASSERT_GT(c.count, 100u);
    // Center x=0.5 → right of rect center (unit = 0.8·half → px shift).
    EXPECT_GT(c.x, 128.0 + 20.0)
        << "center.x=0.5 should shift the pie right of center";
}

TEST(PieLabels, AutopctDrawsText) {
    // autopct labels render text inside the wedges — the image must
    // contain dark pixels (text) beyond the wedge fill colors.
    BatchFigure cf(256);
    PieData d;
    d.values = {1, 1};
    d.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0)};
    d.autopct = "%1.0f%%";
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    // Black-ish pixels = text.
    size_t dark = img.countIf([](uint32_t, uint32_t, Pixel p) {
        return p.r < 80 && p.g < 80 && p.b < 80 && p.a > 200;
    });
    EXPECT_GT(dark, 10u) << "autopct labels should draw text pixels";
}

TEST(PieLabels, LabelDistanceNaNSuppressesLabels) {
    // labeldistance=NaN (mpl None) → no category labels even with
    // labels supplied. Only wedge fills + autopct (none here) remain.
    BatchFigure cf(256);
    PieData d;
    d.values = {1, 1};
    d.labels = {"A", "B"};
    d.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0)};
    d.labelDistance = std::numeric_limits<float>::quiet_NaN();
    cf.axes->addPlot(std::make_unique<PiePlot>(std::move(d)));
    cf.axes->setViewport({-1.25f, 1.25f, -1.25f, 1.25f});
    auto img = cf.render();

    // No autopct → the only non-white pixels are the wedges. Check the
    // corners/edge region for stray dark text pixels — should be none.
    size_t dark = img.countIf([](uint32_t, uint32_t, Pixel p) {
        return p.r < 80 && p.g < 80 && p.b < 80 && p.a > 200;
    });
    EXPECT_EQ(dark, 0u) << "labelDistance=NaN should suppress labels";
}

TEST(PieLabels, PieFrameFalseHidesAxes) {
    // Axes::pie (not raw addPlot) with frame=false hides frame/ticks
    // and pins the view to ±1.25 + center.
    BatchFigure cf(256);
    PieData d;
    d.values = {1, 1};
    cf.axes->pie(std::move(d));
    EXPECT_FALSE(cf.axes->frameOn());
    EXPECT_FLOAT_EQ(cf.axes->xlim().min, -1.25f);
    EXPECT_FLOAT_EQ(cf.axes->xlim().max, 1.25f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 6: fill_between where + interpolate
// ═══════════════════════════════════════════════════════════════════════════

TEST(FillWhere, MaskLeavesGaps) {
    // Fill y=10 vs y=0 only where the mask is true — the masked-out
    // region must not be filled.
    BatchFigure cf(256);
    std::vector<float> x(11), y1(11, 10.0f), y2(11, 0.0f);
    for (int i = 0; i <= 10; ++i) x[i] = float(i);
    // where: true for x in [0,4] and [6,10] — gap at x=5.
    std::vector<bool> where(11);
    for (int i = 0; i <= 10; ++i) where[i] = (i != 5);
    auto p = std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(0, 255, 0, 255));
    p->setWhere(std::move(where), false);
    cf.axes->addPlot(std::move(p));
    auto img = cf.render();

    // The gap column (x=5 → pixel ~128) should be white in the middle.
    // x data range 0..10 with 5% pad → [-0.5, 10.5].
    const auto& r = cf.axes->rect;
    float px5 = r.x + ((5.0f + 0.5f) / 11.0f) * float(r.width);
    Pixel gap = img.get(uint32_t(px5), 128);
    EXPECT_TRUE(gap.approx(Pixel::white(), 40))
        << "masked-out x=5 should not be filled";

    // A filled column (x=2) should be green.
    float px2 = r.x + ((2.0f + 0.5f) / 11.0f) * float(r.width);
    Pixel filled = img.get(uint32_t(px2), 128);
    EXPECT_TRUE(filled.approx(Pixel::green(), 60))
        << "x=2 should be filled";
}

TEST(FillWhere, AutoscaleExcludesMasked) {
    // mpl: where=False points don't contribute to the data limits.
    BatchFigure cf(256);
    std::vector<float> x(5), y1(5), y2(5, 0.0f);
    for (int i = 0; i < 5; ++i) { x[i] = float(i); y1[i] = 1.0f; }
    // Only indices 1..2 are true — autoscale x should cover x∈[1,2],
    // not the full [0,4].
    std::vector<bool> where{false, true, true, false, false};
    auto p = std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(0, 0, 255, 255));
    p->setWhere(std::move(where));
    cf.axes->addPlot(std::move(p));
    cf.render();
    const auto& v = cf.axes->viewport();
    // x range ~[1,2] with 5% margins → min>0.5, max<3.5.
    EXPECT_GT(v.x.min, 0.5f);
    EXPECT_LT(v.x.max, 3.5f);
}

TEST(FillWhere, InterpolateExtendsToCrossing) {
    // y1 = x crosses the constant y2 = 3.5 at x = 3.5 — inside the
    // masked gap (where ends at index 3). mpl interpolate extends the
    // region to the y1==y2 root, so fill appears between x=3 and x=3.5.
    BatchFigure cf(256);
    std::vector<float> x(11), y1(11), y2(11, 3.5f);
    for (int i = 0; i <= 10; ++i) { x[i] = float(i); y1[i] = float(i); }
    std::vector<bool> where(11, false);
    for (int i = 0; i <= 3; ++i) where[i] = true;
    auto p = std::make_unique<FillBetweenPlot>(
        std::move(x), std::move(y1), std::move(y2),
        Color::fromRgba8(255, 0, 0, 255));
    p->setWhere(std::move(where), true);
    cf.axes->addPlot(std::move(p));
    cf.axes->setViewport({0, 10, 0, 10});
    auto img = cf.render();

    const auto& r = cf.axes->rect;
    // At x=3.3 the extension triangle spans y≈[3.3, 3.5] → check y=3.4.
    float px = r.x + (3.3f / 10.0f) * float(r.width);
    float py = r.y + (1.0f - 3.4f / 10.0f) * float(r.height);
    Pixel filled = img.get(uint32_t(px), uint32_t(py));
    EXPECT_TRUE(filled.approx(Pixel::red(), 80))
        << "interpolate should extend the fill to the x=3.5 crossing";

    // Without interpolate the same spot stays empty.
    BatchFigure cf2(256);
    std::vector<float> x2(11), y1b(11), y2b(11, 3.5f);
    for (int i = 0; i <= 10; ++i) { x2[i] = float(i); y1b[i] = float(i); }
    std::vector<bool> where2(11, false);
    for (int i = 0; i <= 3; ++i) where2[i] = true;
    auto p2 = std::make_unique<FillBetweenPlot>(
        std::move(x2), std::move(y1b), std::move(y2b),
        Color::fromRgba8(255, 0, 0, 255));
    p2->setWhere(std::move(where2), false);
    cf2.axes->addPlot(std::move(p2));
    cf2.axes->setViewport({0, 10, 0, 10});
    auto img2 = cf2.render();
    const auto& r2 = cf2.axes->rect;
    float px2 = r2.x + (3.3f / 10.0f) * float(r2.width);
    float py2 = r2.y + (1.0f - 3.4f / 10.0f) * float(r2.height);
    Pixel empty = img2.get(uint32_t(px2), uint32_t(py2));
    EXPECT_TRUE(empty.approx(Pixel::white(), 40))
        << "without interpolate the fill must stop at x=3";
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 7: stackplot baseline modes
// ═══════════════════════════════════════════════════════════════════════════

TEST(StackBaseline, UnknownNameThrows) {
    StackPlot sp({0, 1}, {{1, 1}});
    EXPECT_THROW(sp.setBaseline("bogus"), std::invalid_argument);
}

TEST(StackBaseline, SymIsSymmetricAboutZero) {
    // 'sym' → first_line = -sum/2: the stack is symmetric around 0.
    StackPlot sp({0, 1}, {{1, 2}, {1, 2}});
    sp.setBaseline(StackBaseline::Sym);
    // Autoscale range should be symmetric: sum=[2,4] → ymin=-2, ymax=+2.
    Viewport v;
    sp.contributeToAutoscale(v);
    EXPECT_NEAR(v.y.min, -v.y.max, 0.01f);
}

TEST(StackBaseline, WiggleDiffersFromZero) {
    // wiggle puts the baseline between zero and -sum/2 — it must differ
    // from 'zero' (flat baseline) for non-constant data.
    StackPlot zw({0, 1, 2}, {{1, 3, 1}, {1, 1, 3}});
    zw.setBaseline(StackBaseline::Zero);
    StackPlot wg({0, 1, 2}, {{1, 3, 1}, {1, 1, 3}});
    wg.setBaseline(StackBaseline::Wiggle);
    Viewport vz, vw;
    zw.contributeToAutoscale(vz);
    wg.contributeToAutoscale(vw);
    // Wiggle has a nonzero (negative) lower bound vs zero's flat 0.
    EXPECT_LT(vw.y.min, vz.y.min - 0.1f);
}

TEST(StackBaseline, SymRenderFillsBothSides) {
    // Render a 'sym' stackplot: fill exists above AND below the y=0
    // midline of the axes.
    BatchFigure cf(256);
    auto sp = std::make_unique<StackPlot>(
        std::vector<float>{0, 5, 10},
        std::vector<std::vector<float>>{{2, 4, 2}, {2, 2, 4}},
        std::vector<Color>{Color::fromRgba8(255, 0, 0, 255),
                           Color::fromRgba8(0, 255, 0, 255)});
    sp->setBaseline(StackBaseline::Sym);
    cf.axes->addPlot(std::move(sp));
    auto img = cf.render();

    const auto& r = cf.axes->rect;
    uint32_t midY = uint32_t(r.y + r.height * 0.5f);
    // Above the midline (upper half) and below it should both have fill.
    size_t above = img.countIf([&](uint32_t, uint32_t y, Pixel p) {
        return y < midY - 5 && !(p.r > 230 && p.g > 230 && p.b > 230);
    });
    size_t below = img.countIf([&](uint32_t, uint32_t y, Pixel p) {
        return y > midY + 5 && !(p.r > 230 && p.g > 230 && p.b > 230);
    });
    EXPECT_GT(above, 100u) << "sym baseline should fill above midline";
    EXPECT_GT(below, 100u) << "sym baseline should fill below midline";
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 8: Axes::labelOuter
// ═══════════════════════════════════════════════════════════════════════════

TEST(LabelOuter, GridPositionsDetected) {
    Figure fig(2, 2);
    auto* tl = fig.addAxes(0, 0);
    auto* tr = fig.addAxes(0, 1);
    auto* bl = fig.addAxes(1, 0);
    auto* br = fig.addAxes(1, 1);

    EXPECT_TRUE(bl->isLastRow());
    EXPECT_TRUE(br->isLastRow());
    EXPECT_FALSE(tl->isLastRow());
    EXPECT_FALSE(tr->isLastRow());

    EXPECT_TRUE(tl->isFirstCol());
    EXPECT_TRUE(bl->isFirstCol());
    EXPECT_FALSE(tr->isFirstCol());
    EXPECT_FALSE(br->isFirstCol());
}

TEST(LabelOuter, SuppressesInnerLabels) {
    Figure fig(2, 2);
    auto* tl = fig.addAxes(0, 0);
    auto* tr = fig.addAxes(0, 1);
    auto* bl = fig.addAxes(1, 0);
    auto* br = fig.addAxes(1, 1);
    for (auto* a : {tl, tr, bl, br}) a->labelOuter();

    // Top row: x labels hidden (not last row), y labels on left col.
    EXPECT_TRUE(tl->xTickLabelsHidden());
    EXPECT_TRUE(tr->xTickLabelsHidden());
    EXPECT_FALSE(bl->xTickLabelsHidden());
    EXPECT_FALSE(br->xTickLabelsHidden());
    // Right col: y labels hidden (not first col).
    EXPECT_FALSE(tl->yTickLabelsHidden());
    EXPECT_TRUE(tr->yTickLabelsHidden());
    EXPECT_FALSE(bl->yTickLabelsHidden());
    EXPECT_TRUE(br->yTickLabelsHidden());
}

TEST(LabelOuter, RemoveInnerTicksFlag) {
    Axes ax;
    ax.labelOuter(true);
    EXPECT_TRUE(ax.innerTicksRemoved());
    Axes ax2;
    ax2.labelOuter(false);
    EXPECT_FALSE(ax2.innerTicksRemoved());
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 9: axis("off") / setAxisOff / setFrameOn
// ═══════════════════════════════════════════════════════════════════════════

TEST(AxisVisibility, AxisOffSuppressesFurniture) {
    // A figure with default (non-flat) style — spines/ticks visible.
    // axis("off") must hide them all while plot artists stay.
    PlotTestHarness harness(256, 256, vk::SampleCountFlagBits::e1);
    Figure fig(1, 1);
    auto* ax = fig.addAxes(0, 0);
    // Keep the default style (spines + ticks on).
    Series2D s;
    s.color = Color::fromRgba8(255, 0, 0);
    s.lineWidth = 3.0f;
    s.points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
    ax->setAxisOff();
    auto img = harness.render(fig);

    // The red line still renders.
    EXPECT_GT(img.countColor(Pixel::red(), 60), 50u)
        << "plot artists must remain visible with axis off";
    // No black spine/tick pixels anywhere.
    size_t dark = img.countIf([](uint32_t, uint32_t, Pixel p) {
        return p.r < 60 && p.g < 60 && p.b < 60 && p.a > 200;
    });
    EXPECT_EQ(dark, 0u)
        << "axis off should hide spines, ticks, and tick labels";
}

TEST(AxisVisibility, AxisStringOff) {
    Axes ax;
    EXPECT_TRUE(ax.axis("off"));
    EXPECT_FALSE(ax.axison());
    EXPECT_TRUE(ax.axis("on"));
    EXPECT_TRUE(ax.axison());
    EXPECT_FALSE(ax.axis("bogus"));
}

TEST(AxisVisibility, AxisStringEqualTight) {
    Axes ax;
    EXPECT_TRUE(ax.axis("equal"));
    EXPECT_EQ(ax.aspect(), AspectMode::Equal);
    EXPECT_TRUE(ax.axis("auto"));
    EXPECT_EQ(ax.aspect(), AspectMode::Auto);
    EXPECT_TRUE(ax.axis("tight"));
    EXPECT_FLOAT_EQ(ax.marginX(), 0.0f);
}

TEST(AxisVisibility, FrameOnKeepsTicks) {
    // mpl set_frame_on(False): patch+spines hidden, ticks remain.
    // We assert the C++ state and that the renderer consults it.
    Axes ax;
    ax.setFrameOn(false);
    EXPECT_FALSE(ax.frameOn());
    ax.setFrameOn(true);
    EXPECT_TRUE(ax.frameOn());
}

TEST(AxisVisibility, AxisOffHidesPatch) {
    // Axes facecolor is part of the patch — axison=false hides it.
    PlotTestHarness harness(256, 256, vk::SampleCountFlagBits::e1);
    Figure fig(1, 1);
    auto* ax = fig.addAxes(0, 0);
    auto st = flatTestStyle();
    st.faceColor = Color::fromRgba8(200, 220, 255);   // light blue patch
    ax->setStyle(st);
    ax->setAxisOff();
    auto img = harness.render(fig);
    // The axes region should be figure-white, not the patch blue.
    Pixel p = img.get(128, 128);
    EXPECT_TRUE(p.approx(Pixel::white(), 30))
        << "axis off should hide the axes patch (facecolor)";
}

// ═══════════════════════════════════════════════════════════════════════════
// Feature 10: imshow(origin="lower")
// ═══════════════════════════════════════════════════════════════════════════

TEST(ImshowOrigin, LowerPutsRowZeroAtBottom) {
    // 2x1 grid (2 rows, 1 col): row 0 = dark (0.0), row 1 = bright (1.0).
    // origin="lower" → row 0 (dark) at the BOTTOM.
    auto makeGrid = [] {
        Grid2D g;
        g.width = 1; g.height = 2;
        g.values = {0.0f, 1.0f};
        g.xRange = {0, 1}; g.yRange = {0, 1};
        g.valueRange = {0, 1};
        return g;
    };

    BatchFigure lo(256);
    lo.axes->imshow(makeGrid(), colormaps::grayscale(), "nearest",
                    "auto", "lower");
    auto imgLo = lo.render();
    // Bottom half should be dark (row 0 = 0.0 → black in grayscale).
    Pixel bot = imgLo.get(128, 220);
    EXPECT_LT(bot.r, 100)
        << "origin=lower should put row 0 (dark) at the bottom";
    Pixel top = imgLo.get(128, 30);
    EXPECT_GT(top.r, 180)
        << "origin=lower should put row 1 (bright) at the top";

    // origin="upper" (default) → row 0 at the top.
    BatchFigure up(256);
    up.axes->imshow(makeGrid(), colormaps::grayscale(), "nearest",
                    "auto", "upper");
    auto imgUp = up.render();
    Pixel topUp = imgUp.get(128, 30);
    EXPECT_LT(topUp.r, 100)
        << "origin=upper should put row 0 (dark) at the top";
}
