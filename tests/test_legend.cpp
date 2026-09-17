// tests/test_legend.cpp — §8 Legend features:
// loc/bbox_to_anchor/bbox_transform, ncols/nrows, title, labelcolor,
// spacing params, shadow/fancybox/frameon, draggable flag, rcParams.
#include <gtest/gtest.h>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Events.hpp>
#include <volcano/plot/Rc.hpp>
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <memory>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

const Pixel kRed{255, 0, 0, 255};
const Pixel kGreen{0, 255, 0, 255};   // Color::green() = (0,1,0)
const Pixel kBlue{0, 0, 255, 255};

struct LFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;
    explicit LFig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        // 10% margins on all sides so outside-axes anchors stay visible.
        figure.subplotsAdjust(0.1f, 0.1f, 0.9f, 0.9f, 0.0f, 0.0f);
        axes->setViewport({0, 1, 0, 1});
    }

    /// Add a legend entry whose data point is outside the viewport, so it
    /// uploads a real buffer but rasterizes nothing.
    ScatterPlot* addEntry(std::string lbl, Color c) {
        Series2D s;
        s.label = std::move(lbl);
        s.color = c;
        s.points = {{-10.0f, -10.0f}};
        auto p = std::make_unique<ScatterPlot>(std::move(s));
        auto* raw = p.get();
        axes->addPlot(std::move(p));
        return raw;
    }

    Image render() { return harness.render(figure); }
};

} // namespace

// ─── LegendStyle fields ────────────────────────────────────────────────────

TEST(LegendStyle, MatplotlibFieldDefaults) {
    LegendStyle lg;
    EXPECT_EQ(lg.ncols, 1);
    EXPECT_EQ(lg.nrows, 0);
    EXPECT_TRUE(lg.title.empty());
    EXPECT_FALSE(lg.labelColor.has_value());
    EXPECT_TRUE(lg.frameOn);
    EXPECT_TRUE(lg.fancyBox);
    EXPECT_FALSE(lg.shadow);
    EXPECT_FLOAT_EQ(lg.handleLength, 2.0f);
    EXPECT_FLOAT_EQ(lg.handleTextPad, 0.8f);
    EXPECT_FLOAT_EQ(lg.borderPad, 0.4f);
    EXPECT_FLOAT_EQ(lg.columnSpacing, 2.0f);
    EXPECT_FALSE(lg.draggable);
    EXPECT_EQ(lg.anchorSpace, CoordSystem::Axes);
}

TEST(AxesLegend, ConvenienceEnablesAndConfigures) {
    Axes axes;
    axes.legend().location = "upper left";
    EXPECT_TRUE(axes.style().legend.visible);
    EXPECT_EQ(axes.style().legend.location, "upper left");
}

// ─── loc positions ─────────────────────────────────────────────────────────

TEST(LegendLoc, UpperLeft) {
    LFig cf;
    cf.axes->legend().location = "upper left";
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto bb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(bb.found);
    // Axes rect ≈ (26,26)-(230,230); legend should hug the top-left.
    EXPECT_LT(bb.x0, 60u);
    EXPECT_LT(bb.y0, 60u);
}

TEST(LegendLoc, LowerRight) {
    // Paired comparison: lower-right marker centroid is far right of the
    // lower-left one, both near the bottom edge.
    LFig cf;
    cf.axes->legend().location = "lower right";
    cf.addEntry("Red", Color::red());
    auto rc = cf.render().centroid(kRed, 40);

    LFig cf2;
    cf2.axes->legend().location = "lower left";
    cf2.addEntry("Red", Color::red());
    auto lc = cf2.render().centroid(kRed, 40);

    ASSERT_GT(rc.count, 0u);
    ASSERT_GT(lc.count, 0u);
    EXPECT_GT(rc.x, lc.x + 60);
    EXPECT_GT(rc.y, 190);
    EXPECT_GT(lc.y, 190);
}

TEST(LegendLoc, Center) {
    LFig cf;
    cf.axes->legend().location = "center";
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto c = img.centroid(kRed, 40);
    ASSERT_GT(c.count, 0u);
    EXPECT_NEAR(c.x, 128, 30);
    EXPECT_NEAR(c.y, 128, 30);
}

TEST(LegendLoc, NumericCode) {
    LFig cf;
    cf.axes->legend().location = "3"; // lower left
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto bb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(bb.found);
    EXPECT_LT(bb.x0, 60u);
    EXPECT_GT(bb.y1, 190u);
}

TEST(LegendLoc, RightIsCenterRight) {
    LFig cf;
    cf.axes->legend().location = "right";
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto c = img.centroid(kRed, 40);
    ASSERT_GT(c.count, 0u);
    // Right half of the canvas, vertically centered.
    EXPECT_GT(c.x, 140);
    EXPECT_NEAR(c.y, 128, 40);
}

// ─── bbox_to_anchor / bbox_transform ───────────────────────────────────────

TEST(LegendAnchor, OutsideAxesRight) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "upper left";   // box's upper-left corner goes to anchor
    lg.anchorX = 1.02f;
    lg.anchorY = 1.0f;
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto bb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(bb.found);
    // Legend marker must be right of the axes rect (~x=230 on a 256 canvas).
    EXPECT_GT(bb.x0, 228u);
}

TEST(LegendAnchor, FigureSpaceTransform) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "upper left";
    lg.anchorX = 0.02f;           // figure fraction: top-left corner
    lg.anchorY = 0.98f;
    lg.anchorSpace = CoordSystem::Figure;
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto bb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(bb.found);
    // Figure top-left = inside the 10% margin strip, left of the axes.
    EXPECT_LT(bb.x0, 25u);
    EXPECT_LT(bb.y0, 30u);
}

// ─── ncols / nrows ─────────────────────────────────────────────────────────

TEST(LegendColumns, TwoColumnsSideBySide) {
    LFig cf;
    cf.axes->legend().ncols = 2;
    cf.addEntry("r", Color::red());
    cf.addEntry("g", Color::green());
    cf.addEntry("b", Color::blue());
    cf.addEntry("k", Color::black());
    auto img = cf.render();

    // Column-major: col0 = [red, green], col1 = [blue, black].
    auto rb = img.boundingBox(kRed, 40);
    auto bb = img.boundingBox(kBlue, 40);
    ASSERT_TRUE(rb.found);
    ASSERT_TRUE(bb.found);
    // Blue is in row 0 of column 1 → same y as red, further right.
    EXPECT_NEAR(double(bb.y0), double(rb.y0), 6.0);
    EXPECT_GT(bb.x0, rb.x1 + 10);
}

TEST(LegendColumns, NrowsOneRow) {
    LFig cf;
    cf.axes->legend().nrows = 1;
    cf.addEntry("r", Color::red());
    cf.addEntry("g", Color::green());
    cf.addEntry("b", Color::blue());
    auto img = cf.render();

    auto rb = img.boundingBox(kRed, 40);
    auto gb = img.boundingBox(kGreen, 40);
    auto bb = img.boundingBox(kBlue, 40);
    ASSERT_TRUE(rb.found);
    ASSERT_TRUE(gb.found);
    ASSERT_TRUE(bb.found);
    // All three markers in a single row.
    EXPECT_NEAR(double(rb.y0), double(gb.y0), 4.0);
    EXPECT_NEAR(double(gb.y0), double(bb.y0), 4.0);
    EXPECT_LT(rb.x1, gb.x0);
    EXPECT_LT(gb.x1, bb.x0);
}

// ─── title ─────────────────────────────────────────────────────────────────

TEST(LegendTitle, TitleRowRendersAboveEntries) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "upper left";
    lg.title = "Groups";
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto rb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(rb.found);
    // Dark title text pixels should appear above the first marker row,
    // inside the legend box (upper-left region).
    size_t dark = img.countIf([&](uint32_t x, uint32_t y, Pixel p) {
        return y >= rb.y0 - 24 && y < rb.y0 - 2 &&
               x >= rb.x0 && x < rb.x1 + 80 &&
               p.r < 100 && p.g < 100 && p.b < 100;
    });
    EXPECT_GT(dark, 8u) << "Legend title should render dark text above entries";
}

// ─── labelcolor ────────────────────────────────────────────────────────────

TEST(LegendLabelColor, OverrideMakesTextColored) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "upper left";
    lg.labelColor = Color::blue();
    cf.addEntry("Lbl", Color::red());
    auto img = cf.render();

    // Blue pixels = label text (the handle is a red disc).
    EXPECT_GT(img.countColor(kBlue, 40), 8u)
        << "labelcolor=blue should color the legend text";
}

// ─── spacing params ────────────────────────────────────────────────────────

TEST(LegendSpacing, LongerHandleWidensMarkerRun) {
    auto redCount = [](float handleLen) {
        LFig cf;
        auto& lg = cf.axes->legend();
        lg.location = "upper left";
        lg.handleLength = handleLen;
        Series2D s;
        s.label = "L";
        s.color = Color::red();
        s.points = {{-10.0f, -10.0f}, {-9.0f, -9.0f}}; // outside viewport
        cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
        return cf.render().countColor(kRed, 40);
    };
    EXPECT_GT(redCount(4.0f), redCount(0.8f) + 10u);
}

TEST(LegendSpacing, BorderPadWidensBox) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "upper left";
    lg.borderPad = 2.0f;
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto rb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(rb.found);
    // Large border pad pushes the handle inward from the box top-left.
    EXPECT_GT(rb.x0, 26u + 15u);
    EXPECT_GT(rb.y0, 26u + 15u);
}

// ─── shadow / frameon ──────────────────────────────────────────────────────

TEST(LegendFrame, ShadowDrawsOffsetDarkRect) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.location = "lower right";
    lg.shadow = true;
    cf.addEntry("Red", Color::red());
    auto img = cf.render();

    auto rb = img.boundingBox(kRed, 40);
    ASSERT_TRUE(rb.found);
    // Shadow appears as dim pixels below/right of the legend box.
    size_t shadow = img.countIf([&](uint32_t x, uint32_t y, Pixel p) {
        return y > rb.y1 && y < rb.y1 + 30 && x > rb.x0 &&
               p.r > 60 && p.r < 200 && p.r == p.g && p.g == p.b;
    });
    EXPECT_GT(shadow, 5u) << "legend shadow should render dim pixels";
}

TEST(LegendFrame, FrameOffStillDrawsHandles) {
    LFig cf;
    auto& lg = cf.axes->legend();
    lg.frameOn = false;
    cf.addEntry("Red", Color::red());
    auto img = cf.render();
    EXPECT_GT(img.countColor(kRed, 40), 8u);
}

// ─── rcParams ──────────────────────────────────────────────────────────────

namespace {
struct RcGuard { ~RcGuard() { rc::rcdefaults(); } };
}

TEST(LegendRc, LocAndNcols) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("legend.loc", "upper left"));
    EXPECT_TRUE(rc::set("legend.ncols", "3"));
    EXPECT_EQ(rc::params().legend.location, "upper left");
    EXPECT_EQ(rc::params().legend.ncols, 3);
}

TEST(LegendRc, LabelColorAndInherit) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("legend.labelcolor", "#ff0000"));
    ASSERT_TRUE(rc::params().legend.labelColor.has_value());
    EXPECT_NEAR(rc::params().legend.labelColor->r, 1.0f, 0.01f);
    EXPECT_TRUE(rc::set("legend.labelcolor", "inherit"));
    EXPECT_FALSE(rc::params().legend.labelColor.has_value());
}

TEST(LegendRc, SpacingAndFrameParams) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("legend.handlelength", "3.5"));
    EXPECT_TRUE(rc::set("legend.handletextpad", "1.2"));
    EXPECT_TRUE(rc::set("legend.borderpad", "0.9"));
    EXPECT_TRUE(rc::set("legend.columnspacing", "4.0"));
    EXPECT_TRUE(rc::set("legend.borderaxespad", "0.7"));
    EXPECT_TRUE(rc::set("legend.fancybox", "false"));
    EXPECT_TRUE(rc::set("legend.shadow", "true"));
    EXPECT_TRUE(rc::set("legend.title_fontsize", "16"));
    EXPECT_FLOAT_EQ(rc::params().legend.handleLength, 3.5f);
    EXPECT_FLOAT_EQ(rc::params().legend.handleTextPad, 1.2f);
    EXPECT_FLOAT_EQ(rc::params().legend.borderPad, 0.9f);
    EXPECT_FLOAT_EQ(rc::params().legend.columnSpacing, 4.0f);
    EXPECT_FLOAT_EQ(rc::params().legend.borderAxesPad, 0.7f);
    EXPECT_FALSE(rc::params().legend.fancyBox);
    EXPECT_TRUE(rc::params().legend.shadow);
    EXPECT_FLOAT_EQ(rc::params().legend.titleFont.size, 16.0f);
}

// ─── handler_map (mpl legend handler protocol) ──────────────────────────────

TEST(LegendHandlerMap, HandlerOverridesDefaultEntry) {
    LFig lf(256);
    Series2D s;
    s.label = "line";
    s.color = Color::green();
    s.points = {{-10.0f, -10.0f}, {-9.0f, -9.0f}};
    lf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));

    auto& lg = lf.axes->legend();
    // mpl handler_map: LinePlot → two custom handles (red + blue).
    lg.handlerMap[std::type_index(typeid(LinePlot))] =
        [](const IPlot&) {
            return std::vector<LegendHandle>{
                {"h-red", Color::red(), LegendMarker::Square},
                {"h-blue", Color::blue(), LegendMarker::Square}};
        };
    auto img = lf.render();

    const auto box = lf.axes->legendBox();
    ASSERT_GT(box.width, 0u) << "legend box should be tracked";
    // Both handler colors should appear inside the legend box.
    size_t red = 0, blue = 0;
    for (uint32_t y = uint32_t(box.y); y < uint32_t(box.y + box.height); ++y)
        for (uint32_t x = uint32_t(box.x); x < uint32_t(box.x + box.width); ++x) {
            auto p = img.get(x, y);
            if (p.approx(Pixel::red(), 60)) ++red;
            if (p.approx(Pixel::blue(), 60)) ++blue;
        }
    EXPECT_GT(red, 10u) << "handler's red handle should render";
    EXPECT_GT(blue, 10u) << "handler's blue handle should render";
}

TEST(LegendHandlerMap, UnhandledTypeFallsBackToLabel) {
    LFig lf(256);
    lf.addEntry("scatter", Color::green());
    auto& lg = lf.axes->legend();
    // Handler registered for a different type — scatter falls back.
    lg.handlerMap[std::type_index(typeid(LinePlot))] =
        [](const IPlot&) {
            return std::vector<LegendHandle>{
                {"x", Color::red(), LegendMarker::Square}};
        };
    auto img = lf.render();
    const auto box = lf.axes->legendBox();
    ASSERT_GT(box.width, 0u);
    size_t green = 0;
    for (uint32_t y = uint32_t(box.y); y < uint32_t(box.y + box.height); ++y)
        for (uint32_t x = uint32_t(box.x); x < uint32_t(box.x + box.width); ++x)
            if (img.get(x, y).approx(Pixel::green(), 60)) ++green;
    EXPECT_GT(green, 10u) << "fallback entry should use the plot's color";
}

// ─── Draggable legend (mpl legend.draggable) ────────────────────────────────

TEST(LegendDrag, DraggableMovesBox) {
    LFig lf(256);
    lf.addEntry("a", Color::green());
    lf.axes->legend().draggable = true;
    auto img0 = lf.render();
    const auto box0 = lf.axes->legendBox();
    ASSERT_GT(box0.width, 0u);

    // Press inside the legend, drag +30/+20, release.
    const float cx = box0.x + box0.width / 2.0f;
    const float cy = box0.y + box0.height / 2.0f;
    Event press{Event::Type::ButtonPress};
    press.x = cx; press.y = cy; press.button = 1;
    lf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = cx + 30.0f; move.y = cy + 20.0f; move.buttons = 1;
    lf.figure.dispatch(move);
    Event release{Event::Type::ButtonRelease};
    release.x = move.x; release.y = move.y; release.button = 1;
    lf.figure.dispatch(release);

    const auto& lg = lf.axes->style().legend;
    EXPECT_NEAR(lg.dragOffset.x, 30.0f, 0.01f);
    EXPECT_NEAR(lg.dragOffset.y, 20.0f, 0.01f);

    auto img1 = lf.render();
    const auto box1 = lf.axes->legendBox();
    EXPECT_NEAR(box1.x, box0.x + 30.0f, 1.0f) << "box should move with drag";
    EXPECT_NEAR(box1.y, box0.y + 20.0f, 1.0f);
}

TEST(LegendDrag, NonDraggableIgnoresPress) {
    LFig lf(256);
    lf.addEntry("a", Color::green());
    lf.axes->legend().draggable = false;
    auto img = lf.render();
    const auto box = lf.axes->legendBox();
    ASSERT_GT(box.width, 0u);

    Event press{Event::Type::ButtonPress};
    press.x = box.x + box.width / 2.0f;
    press.y = box.y + box.height / 2.0f;
    press.button = 1;
    lf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = press.x + 40.0f; move.y = press.y + 40.0f; move.buttons = 1;
    lf.figure.dispatch(move);

    const auto& lg = lf.axes->style().legend;
    EXPECT_FLOAT_EQ(lg.dragOffset.x, 0.0f);
    EXPECT_FLOAT_EQ(lg.dragOffset.y, 0.0f);
}

TEST(LegendDrag, PressOutsideLegendStartsNoDrag) {
    LFig lf(256);
    lf.addEntry("a", Color::green());
    lf.axes->legend().draggable = true;
    auto img = lf.render();
    ASSERT_GT(lf.axes->legendBox().width, 0u);

    // Press at the canvas corner — outside the legend box.
    Event press{Event::Type::ButtonPress};
    press.x = 2.0f; press.y = 254.0f; press.button = 1;
    lf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = 60.0f; move.y = 200.0f; move.buttons = 1;
    lf.figure.dispatch(move);

    const auto& lg = lf.axes->style().legend;
    EXPECT_FLOAT_EQ(lg.dragOffset.x, 0.0f);
    EXPECT_FLOAT_EQ(lg.dragOffset.y, 0.0f);
}
