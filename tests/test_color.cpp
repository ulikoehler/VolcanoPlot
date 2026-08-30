// tests/test_color.cpp — tests for Color::parse and ColorCycle
#include <volcano/plot/Types.hpp>

#include <gtest/gtest.h>

using namespace volcano::plot;

// ─── Single-letter shorthands ─────────────────────────────────────────────

TEST(ColorParse, ShorthandBlue) {
    auto c = Color::parse("b");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 0.0f);
    EXPECT_FLOAT_EQ(c->g, 0.0f);
    EXPECT_FLOAT_EQ(c->b, 1.0f);
    EXPECT_FLOAT_EQ(c->a, 1.0f);
}

TEST(ColorParse, ShorthandGreen) {
    auto c = Color::parse("g");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->g, 128.0f / 255.0f);
}

TEST(ColorParse, ShorthandRed) {
    auto c = Color::parse("r");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
}

TEST(ColorParse, ShorthandCyan) {
    auto c = Color::parse("c");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->b, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 1.0f);
}

TEST(ColorParse, ShorthandMagenta) {
    auto c = Color::parse("m");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->b, 1.0f);
}

TEST(ColorParse, ShorthandYellow) {
    auto c = Color::parse("y");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 1.0f);
}

TEST(ColorParse, ShorthandBlack) {
    auto c = Color::parse("k");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 0.0f);
    EXPECT_FLOAT_EQ(c->g, 0.0f);
    EXPECT_FLOAT_EQ(c->b, 0.0f);
}

TEST(ColorParse, ShorthandWhite) {
    auto c = Color::parse("w");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 1.0f);
    EXPECT_FLOAT_EQ(c->b, 1.0f);
}

TEST(ColorParse, ShorthandCaseInsensitive) {
    auto upper = Color::parse("B");
    auto lower = Color::parse("b");
    ASSERT_TRUE(upper.has_value());
    ASSERT_TRUE(lower.has_value());
    EXPECT_FLOAT_EQ(upper->b, lower->b);
}

// ─── Hex colors ────────────────────────────────────────────────────────────

TEST(ColorParse, Hex3RGB) {
    auto c = Color::parse("#f00");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 0.0f);
    EXPECT_FLOAT_EQ(c->b, 0.0f);
}

TEST(ColorParse, Hex3Expanded) {
    auto c = Color::parse("#abc");
    ASSERT_TRUE(c.has_value());
    // Each digit expands: a->0xaa, b->0xbb, c->0xcc
    EXPECT_NEAR(c->r, 0xaa / 255.0f, 0.001f);
    EXPECT_NEAR(c->g, 0xbb / 255.0f, 0.001f);
    EXPECT_NEAR(c->b, 0xcc / 255.0f, 0.001f);
}

TEST(ColorParse, Hex6RRGGBB) {
    auto c = Color::parse("#ff8800");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_NEAR(c->g, 0x88 / 255.0f, 0.001f);
    EXPECT_FLOAT_EQ(c->b, 0.0f);
}

TEST(ColorParse, Hex4RGBA) {
    auto c = Color::parse("#f00f");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->a, 1.0f);
}

TEST(ColorParse, Hex8RRGGBBAA) {
    auto c = Color::parse("#ff000080");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_NEAR(c->a, 0x80 / 255.0f, 0.001f);
}

TEST(ColorParse, HexUppercase) {
    auto c = Color::parse("#FF8800");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
}

TEST(ColorParse, HexInvalid) {
    EXPECT_FALSE(Color::parse("#ggg").has_value());
    EXPECT_FALSE(Color::parse("#12").has_value());
    EXPECT_FALSE(Color::parse("#12345").has_value());
}

// ─── Named colors ──────────────────────────────────────────────────────────

TEST(ColorParse, NamedRed) {
    auto c = Color::parse("red");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 0.0f);
    EXPECT_FLOAT_EQ(c->b, 0.0f);
}

TEST(ColorParse, NamedBlue) {
    auto c = Color::parse("blue");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->b, 1.0f);
}

TEST(ColorParse, NamedCaseInsensitive) {
    auto c1 = Color::parse("Red");
    auto c2 = Color::parse("RED");
    auto c3 = Color::parse("red");
    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());
    ASSERT_TRUE(c3.has_value());
    EXPECT_FLOAT_EQ(c1->r, c3->r);
    EXPECT_FLOAT_EQ(c2->r, c3->r);
}

TEST(ColorParse, NamedLightBlue) {
    auto c = Color::parse("lightblue");
    ASSERT_TRUE(c.has_value());
    EXPECT_NEAR(c->r, 173.0f / 255.0f, 0.001f);
    EXPECT_NEAR(c->g, 216.0f / 255.0f, 0.001f);
    EXPECT_NEAR(c->b, 230.0f / 255.0f, 0.001f);
}

TEST(ColorParse, NamedTabAlias) {
    auto c = Color::parse("tab:blue");
    ASSERT_TRUE(c.has_value());
    auto cycle = ColorCycle::at(0);
    EXPECT_FLOAT_EQ(c->r, cycle.r);
    EXPECT_FLOAT_EQ(c->g, cycle.g);
    EXPECT_FLOAT_EQ(c->b, cycle.b);
}

TEST(ColorParse, NamedNotFound) {
    EXPECT_FALSE(Color::parse("notacolor").has_value());
    EXPECT_FALSE(Color::parse("foobar").has_value());
}

// ─── CN cycle colors ───────────────────────────────────────────────────────

TEST(ColorParse, CycleC0) {
    auto c = Color::parse("C0");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, ColorCycle::at(0).r);
}

TEST(ColorParse, CycleC9) {
    auto c = Color::parse("C9");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, ColorCycle::at(9).r);
}

TEST(ColorParse, CycleCaseInsensitive) {
    auto c = Color::parse("c3");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, ColorCycle::at(3).r);
}

TEST(ColorParse, CycleInvalid) {
    EXPECT_FALSE(Color::parse("C10").has_value());
    EXPECT_FALSE(Color::parse("Cx").has_value());
}

// ─── Grayscale ──────────────────────────────────────────────────────────────

TEST(ColorParse, GrayscaleZero) {
    auto c = Color::parse("0.0");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 0.0f);
    EXPECT_FLOAT_EQ(c->g, 0.0f);
    EXPECT_FLOAT_EQ(c->b, 0.0f);
}

TEST(ColorParse, GrayscaleOne) {
    auto c = Color::parse("1.0");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
    EXPECT_FLOAT_EQ(c->g, 1.0f);
    EXPECT_FLOAT_EQ(c->b, 1.0f);
}

TEST(ColorParse, GrayscaleHalf) {
    auto c = Color::parse("0.5");
    ASSERT_TRUE(c.has_value());
    EXPECT_NEAR(c->r, 0.5f, 0.01f);
    EXPECT_NEAR(c->g, 0.5f, 0.01f);
    EXPECT_NEAR(c->b, 0.5f, 0.01f);
}

TEST(ColorParse, GrayscaleOutOfRange) {
    EXPECT_FALSE(Color::parse("1.5").has_value());
    EXPECT_FALSE(Color::parse("-0.1").has_value());
}

// ─── Edge cases ─────────────────────────────────────────────────────────────

TEST(ColorParse, EmptyString) {
    EXPECT_FALSE(Color::parse("").has_value());
}

TEST(ColorParse, WhitespaceTrimmed) {
    auto c = Color::parse("  red  ");
    ASSERT_TRUE(c.has_value());
    EXPECT_FLOAT_EQ(c->r, 1.0f);
}

TEST(ColorParse, ParseOrFallback) {
    auto c = Color::parseOr("notacolor", Color::green());
    EXPECT_FLOAT_EQ(c.g, Color::green().g);
}

TEST(ColorParse, ParseOrValid) {
    auto c = Color::parseOr("red", Color::green());
    EXPECT_FLOAT_EQ(c.r, 1.0f);
}

// ─── ColorCycle ─────────────────────────────────────────────────────────────

TEST(ColorCycle, HasTenColors) {
    EXPECT_EQ(ColorCycle::size(), 10u);
}

TEST(ColorCycle, WrapsAround) {
    auto c0 = ColorCycle::at(0);
    auto c10 = ColorCycle::at(10);
    EXPECT_FLOAT_EQ(c0.r, c10.r);
    EXPECT_FLOAT_EQ(c0.g, c10.g);
    EXPECT_FLOAT_EQ(c0.b, c10.b);
}

TEST(ColorCycle, DistinctColors) {
    // At least the first few colors should be distinct.
    auto c0 = ColorCycle::at(0);
    auto c1 = ColorCycle::at(1);
    auto c2 = ColorCycle::at(2);
    EXPECT_NE(c0.r, c1.r);
    EXPECT_NE(c1.r, c2.r);
}
