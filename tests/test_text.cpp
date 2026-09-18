// tests/test_text.cpp — §5 text features: MathText layout, connection
// styles, multi-line, clipping, font rotation/alignment.
#include <gtest/gtest.h>
#include <volcano/text/MathText.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>
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
    t->fontSize = 2.0f;  // larger text → more dark pixels
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
