// tests/test_text.cpp — §5 text features: MathText layout, connection
// styles, multi-line, clipping, font rotation/alignment.
#include <volcano/plot/Events.hpp>
#include <gtest/gtest.h>
#include <volcano/text/MathText.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Colormap.hpp>
#include "PlotTestHarness.hpp"

#include <cmath>
#include <string>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {
// Fake measure function: each character is 8px wide, 16px high, ascent 12.
text::TextMeasure fakeMeasure(std::string_view s, float scale) {
    // Count UTF-8 codepoints.
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        i += (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        ++n;
    }
    float px = 16.0f * scale;
    return {n * 8.0f * scale, px, px * 0.75f};
}
} // namespace

// ─── MathText parsing / layout ─────────────────────────────────────────────

TEST(MathText, ContainsMathDetection) {
    EXPECT_FALSE(text::containsMath("hello"));
    EXPECT_TRUE(text::containsMath("$x^2$"));
    EXPECT_TRUE(text::containsMath("a $x$ b"));
}

TEST(MathText, PlainTextSingleRun) {
    auto lay = text::layoutMathText("abc", 1.0f, fakeMeasure);
    ASSERT_EQ(lay.runs.size(), 1u);
    EXPECT_EQ(lay.runs[0].text, "abc");
    EXPECT_NEAR(lay.runs[0].x, 0.0f, 1e-4f);
    EXPECT_NEAR(lay.width, 3 * 8.0f, 1e-4f);
    EXPECT_TRUE(lay.rules.empty());
}

TEST(MathText, GreekLetterMappedToUnicode) {
    auto lay = text::layoutMathText("$\\alpha$", 1.0f, fakeMeasure);
    ASSERT_EQ(lay.runs.size(), 1u);
    EXPECT_EQ(lay.runs[0].text, "α");
}

TEST(MathText, SuperscriptSmallerAndRaised) {
    auto lay = text::layoutMathText("$x^2$", 1.0f, fakeMeasure);
    ASSERT_EQ(lay.runs.size(), 2u);
    EXPECT_EQ(lay.runs[0].text, "x");
    EXPECT_EQ(lay.runs[1].text, "2");
    EXPECT_LT(lay.runs[1].scale, 1.0f);          // smaller
    EXPECT_LT(lay.runs[1].baseline, 0.0f);       // raised (negative = up)
    EXPECT_GT(lay.runs[1].x, lay.runs[0].x);     // right of base
}

TEST(MathText, SubscriptSmallerAndLowered) {
    auto lay = text::layoutMathText("$x_i$", 1.0f, fakeMeasure);
    ASSERT_EQ(lay.runs.size(), 2u);
    EXPECT_EQ(lay.runs[1].text, "i");
    EXPECT_LT(lay.runs[1].scale, 1.0f);
    EXPECT_GT(lay.runs[1].baseline, 0.0f);       // lowered (positive = down)
}

TEST(MathText, SupAndSubOnSameBase) {
    auto lay = text::layoutMathText("$x^2_i$", 1.0f, fakeMeasure);
    ASSERT_EQ(lay.runs.size(), 3u);
    EXPECT_EQ(lay.runs[0].text, "x");
    EXPECT_EQ(lay.runs[1].text, "2");
    EXPECT_EQ(lay.runs[2].text, "i");
}

TEST(MathText, FractionHasRuleAndStackedRuns) {
    auto lay = text::layoutMathText("$\\frac{a}{b}$", 1.0f, fakeMeasure);
    // num "a" + den "b" + rule
    ASSERT_GE(lay.runs.size(), 2u);
    ASSERT_EQ(lay.rules.size(), 1u);
    // Numerator above baseline, denominator below.
    EXPECT_LT(lay.runs[0].baseline, 0.0f);
    EXPECT_GT(lay.runs[1].baseline, 0.0f);
    // Fraction is taller than plain text.
    EXPECT_GT(lay.ascent + lay.descent, 16.0f);
}

TEST(MathText, SqrtHasSignAndOverline) {
    auto lay = text::layoutMathText("$\\sqrt{x}$", 1.0f, fakeMeasure);
    ASSERT_GE(lay.runs.size(), 2u);
    EXPECT_EQ(lay.runs[0].text, "√");
    EXPECT_EQ(lay.runs[1].text, "x");
    ASSERT_GE(lay.rules.size(), 1u);  // overline
}

TEST(MathText, MixedPlainAndMath) {
    auto lay = text::layoutMathText("Area: $\\pi r^2$", 1.0f, fakeMeasure);
    ASSERT_GE(lay.runs.size(), 3u);
    EXPECT_EQ(lay.runs[0].text, "Area: ");
    EXPECT_EQ(lay.runs[1].text, "π");
}

TEST(MathText, BracedGroupAsScript) {
    auto lay = text::layoutMathText("$x^{2y}$", 1.0f, fakeMeasure);
    ASSERT_GE(lay.runs.size(), 2u);
    EXPECT_EQ(lay.runs[1].text, "2y");
    EXPECT_LT(lay.runs[1].scale, 1.0f);
}

TEST(MathText, SymbolCommands) {
    auto lay = text::layoutMathText("$a \\times b \\pm c \\leq d$",
                                    1.0f, fakeMeasure);
    std::string all;
    for (const auto& r : lay.runs) all += r.text;
    EXPECT_NE(all.find("×"), std::string::npos);
    EXPECT_NE(all.find("±"), std::string::npos);
    EXPECT_NE(all.find("≤"), std::string::npos);
}

TEST(MathText, SumWithLimits) {
    auto lay = text::layoutMathText("$\\sum_{i=0}^{n}$", 1.0f, fakeMeasure);
    std::string all;
    for (const auto& r : lay.runs) all += r.text;
    EXPECT_NE(all.find("∑"), std::string::npos);
    EXPECT_GE(lay.runs.size(), 3u);  // ∑ + sub + sup
}

TEST(MathText, UnmatchedDollarIsLiteral) {
    auto lay = text::layoutMathText("a$b", 1.0f, fakeMeasure);
    std::string all;
    for (const auto& r : lay.runs) all += r.text;
    EXPECT_EQ(all, "a$b");
}

TEST(MathText, AccentCombining) {
    auto lay = text::layoutMathText("$\\hat{x}$", 1.0f, fakeMeasure);
    ASSERT_GE(lay.runs.size(), 1u);
    // x + combining circumflex (U+0302).
    EXPECT_NE(lay.runs[0].text.find("x"), std::string::npos);
}

// ─── Connection styles ────────────────────────────────────────────────────

TEST(ConnectionStyle, ParseArc3) {
    auto cs = parseConnectionStyle("arc3,rad=0.3");
    EXPECT_EQ(cs.kind, ConnectionStyle::Kind::Arc3);
    EXPECT_NEAR(cs.rad, 0.3f, 1e-4f);
}

TEST(ConnectionStyle, ParseAngle) {
    auto cs = parseConnectionStyle("angle,angleA=0,angleB=90,rad=10");
    EXPECT_EQ(cs.kind, ConnectionStyle::Kind::Angle);
    EXPECT_NEAR(cs.angleA, 0.0f, 1e-4f);
    EXPECT_NEAR(cs.angleB, 90.0f, 1e-4f);
    EXPECT_NEAR(cs.rad, 10.0f, 1e-4f);
}

TEST(ConnectionStyle, ParseBar) {
    auto cs = parseConnectionStyle("bar,fraction=0.5");
    EXPECT_EQ(cs.kind, ConnectionStyle::Kind::Bar);
    EXPECT_NEAR(cs.fraction, 0.5f, 1e-4f);
}

TEST(ConnectionStyle, UnknownFallsBackToStraight) {
    auto cs = parseConnectionStyle("bogus");
    EXPECT_EQ(cs.kind, ConnectionStyle::Kind::Arc3);
    EXPECT_NEAR(cs.rad, 0.0f, 1e-4f);
}

TEST(ConnectionPath, Arc3ZeroIsStraight) {
    auto cs = parseConnectionStyle("arc3,rad=0");
    auto path = connectionPath({0, 0}, {100, 0}, cs);
    ASSERT_EQ(path.size(), 2u);
    EXPECT_NEAR(path[0].x, 0.0f, 1e-3f);
    EXPECT_NEAR(path[1].x, 100.0f, 1e-3f);
}

TEST(ConnectionPath, Arc3CurvesPerpendicular) {
    auto cs = parseConnectionStyle("arc3,rad=0.3");
    auto path = connectionPath({0, 0}, {100, 0}, cs);
    ASSERT_GT(path.size(), 2u);
    // Midpoint should be displaced perpendicular to AB.
    auto mid = path[path.size() / 2];
    EXPECT_GT(std::abs(mid.y), 5.0f);  // 0.3*100/2 = 15 at midpoint
    // Endpoints preserved.
    EXPECT_NEAR(path.front().x, 0.0f, 1e-3f);
    EXPECT_NEAR(path.back().x, 100.0f, 1e-3f);
}

TEST(ConnectionPath, AngleMakesKink) {
    auto cs = parseConnectionStyle("angle,angleA=0,angleB=90,rad=0");
    // A=(0,0) dir 0° (east), B=(100,50): ray from B at 90° (north, i.e.
    // upward) — kink at (100, 0).
    auto path = connectionPath({0, 0}, {100, 50}, cs);
    ASSERT_GE(path.size(), 3u);
    EXPECT_NEAR(path[1].x, 100.0f, 1.0f);
    EXPECT_NEAR(path[1].y, 0.0f, 1.0f);
}

TEST(ConnectionPath, BarMakesBracket) {
    auto cs = parseConnectionStyle("bar,fraction=0.2");
    auto path = connectionPath({0, 0}, {100, 0}, cs);
    ASSERT_EQ(path.size(), 4u);
    // Middle points offset perpendicular.
    EXPECT_GT(std::abs(path[1].y), 5.0f);
    EXPECT_GT(std::abs(path[2].y), 5.0f);
}

TEST(ConnectionPath, ShrinkPullsEndpoints) {
    auto cs = parseConnectionStyle("arc3,rad=0");
    auto path = connectionPath({0, 0}, {100, 0}, cs, 10.0f, 20.0f);
    EXPECT_NEAR(path.front().x, 10.0f, 1e-3f);
    EXPECT_NEAR(path.back().x, 80.0f, 1e-3f);
}

// ─── Annotation fields ────────────────────────────────────────────────────

TEST(AnnotationFields, ClipOnDefaultsFalse) {
    TextAnnotation t;
    EXPECT_FALSE(t.clipOn);
    Annotation a;
    EXPECT_FALSE(a.clipOn);
}

TEST(AnnotationFields, ConnectionStyleDefaultStraight) {
    Annotation a;
    EXPECT_EQ(a.connection.kind, ConnectionStyle::Kind::Arc3);
    EXPECT_NEAR(a.connection.rad, 0.0f, 1e-4f);
}

// ─── Rendered output ───────────────────────────────────────────────────────

namespace {
struct TFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;
    explicit TFig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
    }
    Image render() { return harness.render(figure); }
};
} // namespace

TEST(TextRegression, MathTextRendersInAnnotation) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.3f, 0.5f, "$x^2 + \\alpha$", CoordSystem::Data);
    t->color = Color::black();
    t->fontSize = 24.0f;  // larger text → more dark pixels
    auto img = cf.render();
    // Math text should render noticeably dark pixels (AA edges are gray,
    // so count anything darker than mid-gray).
    size_t dark = img.countIf([](uint32_t, uint32_t, Pixel p) {
        return int(p.r) + int(p.g) + int(p.b) < 300;
    });
    EXPECT_GT(dark, 80u);
}

TEST(TextRegression, MultiLineTextRendersTwoLines) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "AAA\nBBB", CoordSystem::Axes);
    t->color = Color::black();
    auto img = cf.render();
    // Count dark pixels in the upper vs lower half of the axes rect —
    // two lines should paint in both regions.
    const auto& r = cf.axes->rect;
    uint32_t midY = r.y + r.height / 2;
    size_t top = img.countColorInRegion(Pixel::black(), r.x, r.y,
                                        r.x + r.width, midY - 2, 60);
    size_t bot = img.countColorInRegion(Pixel::black(), r.x, midY + 2,
                                        r.x + r.width, r.y + r.height, 60);
    EXPECT_GT(top, 10u);
    EXPECT_GT(bot, 10u);
}

TEST(TextRegression, CurvedArrowRenders) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.8f, 0.8f, 0.2f, 0.2f, "",
                                CoordSystem::Axes);
    a->arrowStyle = ArrowStyle::Fancy;
    a->arrowColor = Color::black();
    a->arrowWidth = 2.0f;
    a->connection = parseConnectionStyle("arc3,rad=0.4");
    a->shrinkA = 0; a->shrinkB = 0;
    auto img = cf.render();
    // The curve should paint pixels off the straight diagonal.
    EXPECT_GT(img.countColor(Pixel::black(), 60), 30u);
}

// ─── §5.1 MathText expansion: big operators, auto-sized delimiters ─────────

static const text::MathRun* findRun(const text::MathLayout& l,
                                    std::string_view t) {
    for (const auto& r : l.runs)
        if (r.text == t) return &r;
    return nullptr;
}

TEST(MathText, SumIsEnlargedWithStackedLimits) {
    auto lay = text::layoutMathText("$\\sum_{i=0}^{n}$", 1.0f, fakeMeasure);
    auto* op = findRun(lay, "∑");
    ASSERT_NE(op, nullptr);
    EXPECT_GT(op->scale, 1.2f);              // display-size operator
    auto* sup = findRun(lay, "n");
    auto* sub = findRun(lay, "i=0");
    ASSERT_NE(sup, nullptr);
    ASSERT_NE(sub, nullptr);
    EXPECT_LT(sup->baseline, 0.0f);        // above
    EXPECT_GT(sub->baseline, 0.0f);        // below
    // Stacked: limit runs are horizontally centered on the operator,
    // not hung off its right edge.
    EXPECT_LT(sup->x, op->x + op->scale * 16.0f);
    EXPECT_GT(lay.descent, 0.0f);
}

TEST(MathText, IntKeepsSideScripts) {
    auto lay = text::layoutMathText("$\\int_0^1 x$", 1.0f, fakeMeasure);
    auto* op = findRun(lay, "∫");
    ASSERT_NE(op, nullptr);
    EXPECT_GT(op->scale, 1.2f);
    auto* sup = findRun(lay, "1");
    auto* sub = findRun(lay, "0");
    ASSERT_NE(sup, nullptr);
    ASSERT_NE(sub, nullptr);
    // Side scripts: positioned to the right of the operator.
    EXPECT_GT(sup->x, op->x);
    EXPECT_GT(sub->x, op->x);
}

TEST(MathText, LeftRightAutoSizesDelimiters) {
    auto bare = text::layoutMathText("$\\frac{a}{b}$", 1.0f, fakeMeasure);
    auto lay = text::layoutMathText("$\\left(\\frac{a}{b}\\right)$",
                                    1.0f, fakeMeasure);
    auto* l = findRun(lay, "(");
    auto* r = findRun(lay, ")");
    ASSERT_NE(l, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_GT(l->scale, 1.5f);   // grown to cover the fraction
    EXPECT_FLOAT_EQ(l->scale, r->scale);
    // The delimited layout is at least as tall as the bare fraction.
    EXPECT_GE(lay.ascent + lay.descent,
              bare.ascent + bare.descent);
    // Left paren is emitted before the fraction's numerator run.
    auto* num = findRun(lay, "a");
    ASSERT_NE(num, nullptr);
    EXPECT_LT(l->x, num->x);
    EXPECT_GT(r->x, num->x);
}

TEST(MathText, LeftDotProducesNoDelimiter) {
    auto lay = text::layoutMathText("$\\left. x \\right|$", 1.0f,
                                    fakeMeasure);
    EXPECT_EQ(findRun(lay, "."), nullptr);
    EXPECT_NE(findRun(lay, "|"), nullptr);
    EXPECT_NE(findRun(lay, "x"), nullptr);
}

TEST(MathText, BigDelimitersScale) {
    auto lay = text::layoutMathText("$\\big( x \\big)$", 1.0f, fakeMeasure);
    auto* l = findRun(lay, "(");
    ASSERT_NE(l, nullptr);
    EXPECT_GT(l->scale, 1.0f);
}

TEST(MathText, NestedScriptsNest) {
    auto lay = text::layoutMathText("$x^{y^{z}}$", 1.0f, fakeMeasure);
    auto* x = findRun(lay, "x");
    auto* y = findRun(lay, "y");
    auto* z = findRun(lay, "z");
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    ASSERT_NE(z, nullptr);
    EXPECT_FLOAT_EQ(x->scale, 1.0f);
    EXPECT_FLOAT_EQ(y->scale, 0.7f);
    EXPECT_FLOAT_EQ(z->scale, 0.49f);   // nested script shrinks again
    EXPECT_LT(z->baseline, y->baseline);  // z sits above y
}

TEST(MathText, NestedSubscript) {
    auto lay = text::layoutMathText("$x_{i_j}$", 1.0f, fakeMeasure);
    auto* j = findRun(lay, "j");
    ASSERT_NE(j, nullptr);
    EXPECT_FLOAT_EQ(j->scale, 0.49f);
    EXPECT_GT(j->baseline, 0.0f);
}

TEST(TextRegression, SizeBarRendersLowerRight) {
    TFig cf(256);
    cf.axes->setViewport({0, 10, 0, 10, 0, 1});
    SizeBar sb;
    sb.size = 3.0f;
    sb.label = "3";
    sb.loc = "lower right";
    cf.axes->addSizeBar(sb);
    auto img = cf.render();
    const auto& r = cf.axes->rect;
    // Bar + label should paint dark pixels in the lower-right quadrant.
    size_t n = img.countColorInRegion(
        Pixel::black(), r.x + r.width / 2, r.y + r.height / 2,
        r.x + r.width, r.y + r.height, 80);
    EXPECT_GT(n, 20u);
}

// ─── Draggable annotations (mpl Annotation.draggable / Text.draggable) ─────

TEST(AnnotationDrag, DraggableTextMoves) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "dragme", CoordSystem::Axes);
    t->color = Color::black();
    t->draggable = true;
    auto img0 = cf.render();
    const auto box0 = t->drawBox;
    ASSERT_GT(box0.width, 0u);

    const float cx = box0.x + box0.width / 2.0f;
    const float cy = box0.y + box0.height / 2.0f;
    Event press{Event::Type::ButtonPress};
    press.x = cx; press.y = cy; press.button = 1;
    cf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = cx + 40.0f; move.y = cy + 30.0f; move.buttons = 1;
    cf.figure.dispatch(move);
    Event release{Event::Type::ButtonRelease};
    release.x = move.x; release.y = move.y; release.button = 1;
    cf.figure.dispatch(release);

    EXPECT_NEAR(t->dragOffset.x, 40.0f, 0.01f);
    EXPECT_NEAR(t->dragOffset.y, 30.0f, 0.01f);
    auto img1 = cf.render();
    EXPECT_NEAR(t->drawBox.x, box0.x + 40.0f, 1.0f);
    EXPECT_NEAR(t->drawBox.y, box0.y + 30.0f, 1.0f);
}

TEST(AnnotationDrag, NonDraggableIgnoresPress) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.5f, 0.5f, "still", CoordSystem::Axes);
    t->color = Color::black();
    cf.render();
    ASSERT_GT(t->drawBox.width, 0u);
    Event press{Event::Type::ButtonPress};
    press.x = t->drawBox.x + 2.0f; press.y = t->drawBox.y + 2.0f;
    press.button = 1;
    cf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = press.x + 50.0f; move.y = press.y + 50.0f; move.buttons = 1;
    cf.figure.dispatch(move);
    EXPECT_FLOAT_EQ(t->dragOffset.x, 0.0f);
    EXPECT_FLOAT_EQ(t->dragOffset.y, 0.0f);
}

TEST(AnnotationDrag, ArrowFollowsDraggedText) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* a = cf.axes->annotate(0.8f, 0.8f, 0.3f, 0.3f, "pt",
                                CoordSystem::Axes);
    a->color = Color::black();
    a->arrowColor = Color::black();
    a->draggable = true;
    cf.render();
    const auto box0 = a->drawBox;
    ASSERT_GT(box0.width, 0u);
    const float cx = box0.x + 2.0f, cy = box0.y + 2.0f;
    Event press{Event::Type::ButtonPress};
    press.x = cx; press.y = cy; press.button = 1;
    cf.figure.dispatch(press);
    Event move{Event::Type::MotionNotify};
    move.x = cx - 30.0f; move.y = cy - 30.0f; move.buttons = 1;
    cf.figure.dispatch(move);
    Event rel{Event::Type::ButtonRelease};
    rel.x = move.x; rel.y = move.y; rel.button = 1;
    cf.figure.dispatch(rel);
    EXPECT_NEAR(a->dragOffset.x, -30.0f, 0.01f);
    cf.render();
    EXPECT_NEAR(a->drawBox.x, box0.x - 30.0f, 1.0f);
}

// ─── AnchoredText (mpl_toolkits AnchoredText) ──────────────────────────────

TEST(AnchoredText, RendersAtLoc) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto& at = cf.axes->addAnchoredText("hello", "upper left");
    at.color = Color::black();
    auto img = cf.render();
    const auto& r = cf.axes->rect;
    // Upper-left quadrant should hold the text + frame.
    size_t n = img.countColorInRegion(Pixel::black(), r.x, r.y,
                                      r.x + r.width / 2,
                                      r.y + r.height / 2, 100);
    EXPECT_GT(n, 10u);
}

TEST(AnchoredText, LayoutHonorsLoc) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto& at = cf.axes->addAnchoredText("x", "lower right");
    auto m = [](std::string_view t, float pt) {
        // measure takes points; report ~8px/em at 96dpi proportions.
        return SizeBarTextMeasure{float(t.size()) * pt * 0.6f * 96.0f / 72.0f,
                                  pt * 96.0f / 72.0f, pt * 96.0f / 72.0f * 0.8f};
    };
    auto L = layoutAnchoredText(at, *cf.axes, cf.axes->rect, 96.0f, m);
    ASSERT_TRUE(L.valid);
    const auto& r = cf.axes->rect;
    // Lower-right: box right/bottom near the rect's right/bottom edges.
    EXPECT_GT(L.box.x + L.box.w, r.x + r.width * 0.8f);
    EXPECT_GT(L.box.y + L.box.h, r.y + r.height * 0.8f);
}

TEST(ColorblindCycle, OkabeItoSwap) {
    FigureStyle s = styles::defaultStyle();
    s.colorblindSafe();
    ASSERT_EQ(s.colorCycle.size(), colormaps::okabe_ito().stops.size());
    // Okabe-Ito first color: #E69F00.
    auto c = s.colorCycle.at(0);
    EXPECT_NEAR(c.r, 0xE6 / 255.0f, 0.01f);
    EXPECT_NEAR(c.g, 0x9F / 255.0f, 0.01f);
    EXPECT_NEAR(c.b, 0x00 / 255.0f, 0.01f);
}

TEST(MathText, FontsetTagsMathRuns) {
    // dejavusans (default): math runs keep face 0.
    auto lay = text::layoutMathText("$x^2$", 1.0f, fakeMeasure);
    ASSERT_FALSE(lay.runs.empty());
    for (const auto& r : lay.runs) EXPECT_EQ(r.face, 0);

    // dejavuserif: math runs tagged face 1, plain runs stay face 0.
    lay = text::layoutMathText("v = $\\alpha x$", 1.0f, fakeMeasure,
                               text::MathFontset::DejaVuSerif);
    bool sawSerif = false, sawPlain = false;
    for (const auto& r : lay.runs) {
        if (r.face == 1) sawSerif = true; else sawPlain = true;
    }
    EXPECT_TRUE(sawSerif);
    EXPECT_TRUE(sawPlain);   // "v = " outside $...$ keeps the primary face
}

TEST(MathText, FontsetAltMeasure) {
    // The alt measurer must be used for serif-tagged runs (serif metrics
    // differ). Give it a wider advance so the effect is observable.
    text::MeasureFn serifMeasure = [](std::string_view s, float sc) {
        auto m = fakeMeasure(s, sc);
        m.width *= 2.0f;
        return m;
    };
    auto sans = text::layoutMathText("$xx$", 1.0f, fakeMeasure,
                                     text::MathFontset::DejaVuSerif);
    auto serif = text::layoutMathText("$xx$", 1.0f, fakeMeasure,
                                      text::MathFontset::DejaVuSerif,
                                      serifMeasure);
    EXPECT_NEAR(serif.width, sans.width * 2.0f, 1e-4f);
}

TEST(MathText, ParseFontsetFallback) {
    EXPECT_EQ(text::parseMathFontset("dejavusans"),
              text::MathFontset::DejaVuSans);
    EXPECT_EQ(text::parseMathFontset("dejavuserif"),
              text::MathFontset::DejaVuSerif);
    // Unavailable mpl fontsets degrade gracefully to DejaVuSans.
    for (const char* n : {"cm", "stix", "stixsans", "custom", "bogus"})
        EXPECT_EQ(text::parseMathFontset(n), text::MathFontset::DejaVuSans);
}

TEST(TextRegression, MathTextSerifFontsetDiffers) {
    // Same math annotation rendered with both fontsets — DejaVu Serif
    // glyphs differ visibly from DejaVu Sans, so the images must differ.
    auto renderWith = [](const char* fontset) {
        TFig cf(256);
        cf.figure.style().mathFontset = fontset;
        cf.axes->setViewport({0, 1, 0, 1, 0, 1});
        auto* t = cf.axes->text(0.25f, 0.5f, "$\\alpha x^2$",
                                CoordSystem::Data);
        t->color = Color::black();
        t->fontSize = 24.0f;
        return cf.render();
    };
    auto sans = renderWith("dejavusans");
    auto serif = renderWith("dejavuserif");
    size_t diff = 0;
    for (uint32_t y = 0; y < sans.height(); ++y)
        for (uint32_t x = 0; x < sans.width(); ++x)
            if (!sans.get(x, y).approx(serif.get(x, y), 10)) ++diff;
    EXPECT_GT(diff, 100u);   // serif glyphs → visibly different pixels
    // Both must actually render math text (not blank).
    auto dark = [](const Image& i) {
        return i.countIf([](uint32_t, uint32_t, Pixel p) {
            return p.r < 100;
        });
    };
    EXPECT_GT(dark(sans), 100u);
    EXPECT_GT(dark(serif), 100u);
}
