// tests/test_markers.cpp — §6 markers & line styles
#include "PlotTestHarness.hpp"

#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {

struct CraftedFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit CraftedFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        // Zero margins so the axes rect fills the whole canvas
        // (renderFrame re-runs layout(), so a manual rect override would
        // be overwritten).
        figure.subplotsAdjust(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    }

    Image render() {
        auto img = harness.render(figure);
        auto* info = testing::UnitTest::GetInstance()->current_test_info();
        std::string name = std::string("/tmp/volcano_test_") +
                           info->test_suite_name() + "_" + info->name() + ".png";
        img.save(name);
        return img;
    }
};

void addMarker(CraftedFigure& cf, MarkerStyle m, float size = 24.0f,
               MarkerFill fill = MarkerFill::Full) {
    Series2D s;
    s.color = Color::black();
    s.marker = m;
    s.markerFill = fill;
    s.size = size;
    s.points.push_back({0.5f, 0.5f});
    cf.axes->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
}

} // namespace

// ── Parsing ─────────────────────────────────────────────────────────────────

TEST(MarkerParse, SingleChars) {
    EXPECT_EQ(markerFromChar('o'), MarkerStyle::Circle);
    EXPECT_EQ(markerFromChar('s'), MarkerStyle::Square);
    EXPECT_EQ(markerFromChar('^'), MarkerStyle::Triangle);
    EXPECT_EQ(markerFromChar('v'), MarkerStyle::TriDown);
    EXPECT_EQ(markerFromChar('<'), MarkerStyle::TriLeft);
    EXPECT_EQ(markerFromChar('>'), MarkerStyle::TriRight);
    EXPECT_EQ(markerFromChar('p'), MarkerStyle::Pentagon);
    EXPECT_EQ(markerFromChar('P'), MarkerStyle::PlusFilled);
    EXPECT_EQ(markerFromChar('h'), MarkerStyle::Hexagon1);
    EXPECT_EQ(markerFromChar('H'), MarkerStyle::Hexagon2);
    EXPECT_EQ(markerFromChar('8'), MarkerStyle::Octagon);
    EXPECT_EQ(markerFromChar('x'), MarkerStyle::X);
    EXPECT_EQ(markerFromChar('X'), MarkerStyle::XFilled);
    EXPECT_EQ(markerFromChar('*'), MarkerStyle::Star);
    EXPECT_EQ(markerFromChar('|'), MarkerStyle::VLine);
    EXPECT_EQ(markerFromChar('_'), MarkerStyle::HLine);
    EXPECT_EQ(markerFromChar('.'), MarkerStyle::Point);
    EXPECT_EQ(markerFromChar('d'), MarkerStyle::ThinDiamond);
    EXPECT_EQ(markerFromChar('D'), MarkerStyle::Diamond);
    EXPECT_FALSE(markerFromChar('q').has_value());
    EXPECT_FALSE(markerFromChar(' ').has_value());
}

TEST(MarkerParse, FillStyles) {
    EXPECT_EQ(markerFillFromString("full"), MarkerFill::Full);
    EXPECT_EQ(markerFillFromString("left"), MarkerFill::Left);
    EXPECT_EQ(markerFillFromString("right"), MarkerFill::Right);
    EXPECT_EQ(markerFillFromString("bottom"), MarkerFill::Bottom);
    EXPECT_EQ(markerFillFromString("top"), MarkerFill::Top);
    EXPECT_EQ(markerFillFromString("none"), MarkerFill::None);
    EXPECT_EQ(markerFillFromString("LEFT"), MarkerFill::Left);
    EXPECT_FALSE(markerFillFromString("half").has_value());
}

TEST(LineStyleParse, NamedAndSymbolic) {
    EXPECT_EQ(lineStyleFromString("-"), LineStyle::Solid);
    EXPECT_EQ(lineStyleFromString("solid"), LineStyle::Solid);
    EXPECT_EQ(lineStyleFromString("--"), LineStyle::Dashed);
    EXPECT_EQ(lineStyleFromString("dashed"), LineStyle::Dashed);
    EXPECT_EQ(lineStyleFromString("-."), LineStyle::DashDot);
    EXPECT_EQ(lineStyleFromString("dashdot"), LineStyle::DashDot);
    EXPECT_EQ(lineStyleFromString(":"), LineStyle::Dotted);
    EXPECT_EQ(lineStyleFromString("dotted"), LineStyle::Dotted);
    EXPECT_EQ(lineStyleFromString("none"), LineStyle::None);
    EXPECT_EQ(lineStyleFromString(""), LineStyle::None);
    EXPECT_FALSE(lineStyleFromString("wiggly").has_value());
}

TEST(LineStyleParse, DashPatternScalesWithWidth) {
    auto d1 = dashPattern(LineStyle::Dashed, 1.0f);
    auto d2 = dashPattern(LineStyle::Dashed, 2.0f);
    ASSERT_EQ(d1.size(), 2u);
    EXPECT_NEAR(d1[0], 3.7f, 1e-4f);
    EXPECT_NEAR(d2[0], 7.4f, 1e-4f);
    EXPECT_TRUE(dashPattern(LineStyle::Solid, 1.0f).empty());
    EXPECT_TRUE(dashPattern(LineStyle::None, 1.0f).empty());
    auto dd = dashPattern(LineStyle::DashDot, 1.0f);
    EXPECT_EQ(dd.size(), 4u);
}

TEST(LineStyleParse, JoinCapDrawStyle) {
    EXPECT_EQ(joinStyleFromString("miter"), JoinStyle::Miter);
    EXPECT_EQ(joinStyleFromString("round"), JoinStyle::Round);
    EXPECT_EQ(joinStyleFromString("bevel"), JoinStyle::Bevel);
    EXPECT_EQ(capStyleFromString("butt"), CapStyle::Butt);
    EXPECT_EQ(capStyleFromString("round"), CapStyle::Round);
    EXPECT_EQ(capStyleFromString("projecting"), CapStyle::Projecting);
    EXPECT_EQ(drawStyleFromString("default"), DrawStyle::Default);
    EXPECT_EQ(drawStyleFromString("steps"), DrawStyle::StepsPre);
    EXPECT_EQ(drawStyleFromString("steps-pre"), DrawStyle::StepsPre);
    EXPECT_EQ(drawStyleFromString("steps-mid"), DrawStyle::StepsMid);
    EXPECT_EQ(drawStyleFromString("steps-post"), DrawStyle::StepsPost);
    EXPECT_FALSE(drawStyleFromString("zigzag").has_value());
}

// ── Draw style expansion ────────────────────────────────────────────────────

TEST(DrawStyle, StepsPreInsertsHorizontalThenVertical) {
    std::vector<Point2D> pts{{0, 0}, {1, 1}, {2, 0}};
    auto out = applyDrawStyle(pts, DrawStyle::StepsPre);
    ASSERT_EQ(out.size(), 5u);
    // steps-pre: horizontal first — (0,0) → (0,1)? No: y continues left,
    // so at x of b the y jumps. Pre: (a.x,b.y) mid vertex.
    EXPECT_FLOAT_EQ(out[1].x, 0.0f);
    EXPECT_FLOAT_EQ(out[1].y, 1.0f);
}

TEST(DrawStyle, StepsPostInsertsVerticalThenHorizontal) {
    std::vector<Point2D> pts{{0, 0}, {1, 1}};
    auto out = applyDrawStyle(pts, DrawStyle::StepsPost);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[1].x, 1.0f);
    EXPECT_FLOAT_EQ(out[1].y, 0.0f);
}

TEST(DrawStyle, StepsMidStepsAtMidpoint) {
    std::vector<Point2D> pts{{0, 0}, {2, 1}};
    auto out = applyDrawStyle(pts, DrawStyle::StepsMid);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_FLOAT_EQ(out[1].x, 1.0f);
    EXPECT_FLOAT_EQ(out[1].y, 0.0f);
    EXPECT_FLOAT_EQ(out[2].x, 1.0f);
    EXPECT_FLOAT_EQ(out[2].y, 1.0f);
}

// ── Stroker ─────────────────────────────────────────────────────────────────

TEST(Stroke, SolidLineProducesTriangles) {
    std::vector<Point2D> pts{{0, 0}, {10, 0}};
    StrokeParams sp{.width = 2.0f};
    auto m = strokePolyline(pts, sp);
    EXPECT_GT(m.triCount(), 0u);
}

TEST(Stroke, WiderLineCoversMoreArea) {
    std::vector<Point2D> pts{{0, 0}, {100, 0}};
    auto thin = strokePolyline(pts, StrokeParams{.width = 1.0f});
    auto thick = strokePolyline(pts, StrokeParams{.width = 10.0f});
    // Both have 2 tris; check vertex spread in y.
    auto spanY = [](const TriMesh& m) {
        float mn = 1e9f, mx = -1e9f;
        for (auto& v : m.verts) { mn = std::min(mn, v.y); mx = std::max(mx, v.y); }
        return mx - mn;
    };
    EXPECT_NEAR(spanY(thin), 1.0f, 0.01f);
    EXPECT_NEAR(spanY(thick), 10.0f, 0.01f);
}

TEST(Stroke, ProjectingCapExtendsBeyondEnds) {
    std::vector<Point2D> pts{{10, 0}, {20, 0}};
    auto butt = strokePolyline(pts, StrokeParams{.width = 4.0f,
                                                 .cap = CapStyle::Butt});
    auto proj = strokePolyline(pts, StrokeParams{.width = 4.0f,
                                                 .cap = CapStyle::Projecting});
    float projMaxX = 0.0f;
    for (auto& v : proj.verts) projMaxX = std::max(projMaxX, v.x);
    float buttMaxX = 0.0f;
    for (auto& v : butt.verts) buttMaxX = std::max(buttMaxX, v.x);
    EXPECT_NEAR(projMaxX - buttMaxX, 2.0f, 0.01f);  // half-width extension
}

TEST(Stroke, RoundJoinAddsExtraTriangles) {
    std::vector<Point2D> pts{{0, 0}, {10, 0}, {10, 10}};
    auto bevel = strokePolyline(pts, StrokeParams{.width = 4.0f,
                                                  .join = JoinStyle::Bevel});
    auto round = strokePolyline(pts, StrokeParams{.width = 4.0f,
                                                  .join = JoinStyle::Round});
    EXPECT_GT(round.triCount(), bevel.triCount());
}

TEST(Stroke, DashSplitProducesRuns) {
    std::vector<Point2D> pts{{0, 0}, {100, 0}};
    std::vector<float> dashes{10.0f, 10.0f};
    auto runs = dashSplit(pts, dashes, 0.0f);
    // 100px line, 10 on / 10 off → 5 dashes.
    ASSERT_EQ(runs.size(), 5u);
    EXPECT_NEAR(runs[0].front().x, 0.0f, 1e-3f);
    EXPECT_NEAR(runs[0].back().x, 10.0f, 1e-3f);
}

TEST(Stroke, DashOffsetShiftsPattern) {
    std::vector<Point2D> pts{{0, 0}, {100, 0}};
    std::vector<float> dashes{10.0f, 10.0f};
    auto runs = dashSplit(pts, dashes, 10.0f);  // start in off-phase
    // First run should start at x=10 (after 10px gap).
    ASSERT_FALSE(runs.empty());
    EXPECT_NEAR(runs[0].front().x, 10.0f, 1e-3f);
}

TEST(Stroke, DashedMeshCoversHalfTheLength) {
    std::vector<Point2D> pts{{0, 0}, {100, 0}};
    std::vector<float> dashes{10.0f, 10.0f};
    auto runs = dashSplit(pts, dashes, 0.0f);
    float total = 0.0f;
    for (const auto& r : runs)
        for (size_t i = 1; i < r.size(); ++i)
            total += std::hypot(r[i].x - r[i-1].x, r[i].y - r[i-1].y);
    EXPECT_NEAR(total, 50.0f, 1.0f);  // half of 100px painted
}

TEST(Stroke, NaNSplitsPolyline) {
    std::vector<Point2D> pts{{0, 0}, {10, 0},
                             {std::nanf(""), 0}, {20, 0}, {30, 0}};
    auto m = strokePolyline(pts, StrokeParams{.width = 2.0f});
    // Two runs → 4 triangles.
    EXPECT_EQ(m.triCount(), 4u);
}

// ── Render regression ───────────────────────────────────────────────────────

TEST(MarkerRegression, SquareMarkerHasFilledCorners) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Square, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // Square half-extent is 0.75*12 = 9px → edges at [23,41]. Corners
    // inside that box must be filled (a circle would leave them empty).
    EXPECT_TRUE(dark(img.get(24, 24)));
    EXPECT_TRUE(dark(img.get(40, 24)));
    EXPECT_TRUE(dark(img.get(24, 40)));
    EXPECT_TRUE(dark(img.get(40, 40)));
}

TEST(MarkerRegression, CircleMarkerHasEmptyCorners) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Circle, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // Corners of the 24px bbox should be empty for a circle.
    EXPECT_FALSE(dark(img.get(21, 21)));
    EXPECT_FALSE(dark(img.get(42, 21)));
    EXPECT_TRUE(dark(img.get(32, 32)));
}

TEST(MarkerRegression, TriangleMarkerIsPointyAtTop) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Triangle, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // Apex (top center) dark; top corners empty.
    EXPECT_TRUE(dark(img.get(32, 24)));
    EXPECT_FALSE(dark(img.get(20, 24)));
    EXPECT_FALSE(dark(img.get(44, 24)));
    // Base spans wide (base edge ≈ y 33-36, x 24-39).
    EXPECT_TRUE(dark(img.get(24, 34)));
    EXPECT_TRUE(dark(img.get(38, 34)));
}

TEST(MarkerRegression, PlusMarkerHasArms) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Plus, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    EXPECT_TRUE(dark(img.get(32, 32)));   // center
    EXPECT_TRUE(dark(img.get(24, 32)));   // left arm
    EXPECT_TRUE(dark(img.get(32, 24)));   // top arm
    EXPECT_FALSE(dark(img.get(24, 24)));  // diagonal empty
}

TEST(MarkerRegression, LeftFillOnlyCoversLeftHalf) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Circle, 24.0f, MarkerFill::Left);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    EXPECT_TRUE(dark(img.get(26, 32)));   // left half
    EXPECT_FALSE(dark(img.get(38, 32)));  // right half empty
}

TEST(MarkerRegression, NoneFillLeavesHollowCenter) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Circle, 24.0f, MarkerFill::None);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    EXPECT_FALSE(dark(img.get(32, 32)));  // hollow center
    EXPECT_TRUE(dark(img.get(32, 20)));   // outline ring
}

TEST(MarkerRegression, PentagonRenders) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::Pentagon, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    EXPECT_TRUE(dark(img.get(32, 32)));
    EXPECT_TRUE(dark(img.get(32, 22)));   // top vertex
}

TEST(MarkerRegression, VLineMarkerIsVerticalStroke) {
    CraftedFigure cf(64);
    addMarker(cf, MarkerStyle::VLine, 24.0f);
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    EXPECT_TRUE(dark(img.get(32, 24)));
    EXPECT_TRUE(dark(img.get(32, 40)));
    EXPECT_FALSE(dark(img.get(24, 32)));  // nothing left/right
}

// ── Line style regression ───────────────────────────────────────────────────

TEST(LineStyleRegression, DashedLineHasGaps) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.lineStyle = LineStyle::Dashed;
    s.lineWidth = 2.0f;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();

    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    int darkCount = 0;
    for (int x = 0; x < 128; ++x)
        if (dark(img.get(x, 64))) ++darkCount;
    // Dashed line covers roughly half the row.
    EXPECT_GT(darkCount, 10);
    EXPECT_LT(darkCount, 115);
}

TEST(LineStyleRegression, SolidLineIsContinuous) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.lineStyle = LineStyle::Solid;
    s.lineWidth = 2.0f;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    int darkCount = 0;
    for (int x = 0; x < 128; ++x)
        if (dark(img.get(x, 64))) ++darkCount;
    EXPECT_GT(darkCount, 120);
}

TEST(LineStyleRegression, CustomDashTuple) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.dashes = {20.0f, 20.0f};   // custom (on, off) in px
    s.lineWidth = 2.0f;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // First dash covers x∈[0,20), gap at x=30.
    EXPECT_TRUE(dark(img.get(10, 64)));
    EXPECT_FALSE(dark(img.get(30, 64)));
    EXPECT_TRUE(dark(img.get(50, 64)));
}

TEST(LineStyleRegression, GapColorFillsDashGaps) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.dashes = {16.0f, 16.0f};
    s.gapColor = Color::red();
    s.lineWidth = 3.0f;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    auto reddish = [](Pixel p) { return p.r > 150 && p.g < 100 && p.b < 100; };
    // In the gap at x=24 (off phase), red underlay shows.
    EXPECT_TRUE(dark(img.get(8, 64)));
    EXPECT_TRUE(reddish(img.get(24, 64)));
}

TEST(LineStyleRegression, StepsPostProducesStaircase) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.drawStyle = DrawStyle::StepsPost;
    s.lineWidth = 2.0f;
    s.points = {{0.0f, 0.2f}, {0.5f, 0.8f}, {1.0f, 0.8f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // steps-post: from (0,0.2) vertical jump at x=0.5 then horizontal.
    // At x=0.75 (px 96), y should be 0.8 → pixel row ≈ 128*(1-0.8) = 25.6.
    EXPECT_TRUE(dark(img.get(96, 26)));
    // Before x=0.5 (px 32), y=0.2 → row ≈ 128*0.8 = 102.
    EXPECT_TRUE(dark(img.get(32, 102)));
    // And at px 96, row 102 should be empty (no diagonal).
    EXPECT_FALSE(dark(img.get(96, 102)));
}

TEST(LineStyleRegression, WideLineIsThick) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.lineWidth = 12.0f;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    // 12px wide line centered at row 64: dark from ~58 to ~70.
    EXPECT_TRUE(dark(img.get(64, 59)));
    EXPECT_TRUE(dark(img.get(64, 69)));
    EXPECT_FALSE(dark(img.get(64, 55)));
    EXPECT_FALSE(dark(img.get(64, 73)));
}

TEST(LineStyleRegression, LineStyleNoneRendersNothing) {
    CraftedFigure cf(128);
    Series2D s;
    s.color = Color::black();
    s.lineStyle = LineStyle::None;
    s.points = {{0.0f, 0.5f}, {1.0f, 0.5f}};
    cf.axes->addPlot(std::make_unique<LinePlot>(std::move(s)));
    cf.axes->setViewport({0, 1, 0, 1});
    auto img = cf.render();
    auto dark = [](Pixel p) { return p.r + p.g + p.b < 200; };
    int darkCount = 0;
    for (int x = 0; x < 128; ++x)
        for (int y = 0; y < 128; ++y)
            if (dark(img.get(x, y))) ++darkCount;
    EXPECT_EQ(darkCount, 0);
}
