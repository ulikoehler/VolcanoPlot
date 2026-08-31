// tests/test_styles.cpp — tests for matplotlib style presets
#include <gtest/gtest.h>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Types.hpp>

#include <algorithm>

using namespace volcano::plot;

// ─── Helper: check a color matches an RGB hex value ───────────────────────
static void expectColorHex(const Color& c, const char* hexStr, float tol = 0.01f) {
    auto hv = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return 0;
    };
    int r = hv(hexStr[0]) * 16 + hv(hexStr[1]);
    int g = hv(hexStr[2]) * 16 + hv(hexStr[3]);
    int b = hv(hexStr[4]) * 16 + hv(hexStr[5]);
    EXPECT_NEAR(c.r, r / 255.0f, tol) << "Red mismatch for " << hexStr;
    EXPECT_NEAR(c.g, g / 255.0f, tol) << "Green mismatch for " << hexStr;
    EXPECT_NEAR(c.b, b / 255.0f, tol) << "Blue mismatch for " << hexStr;
}

// ─── default style ────────────────────────────────────────────────────────
TEST(StyleDefault, HasTab10Cycle) {
    auto s = styles::defaultStyle();
    EXPECT_EQ(s.colorCycle.size(), 10u);
    // C0 = tab:blue = 1F77B4
    expectColorHex(s.colorCycle.at(0), "1F77B4");
    // C1 = tab:orange = FF7F0E
    expectColorHex(s.colorCycle.at(1), "FF7F0E");
}

TEST(StyleDefault, WhiteBackground) {
    auto s = styles::defaultStyle();
    EXPECT_NEAR(s.faceColor.r, 1.0f, 0.01f);
    EXPECT_NEAR(s.faceColor.g, 1.0f, 0.01f);
    EXPECT_NEAR(s.faceColor.b, 1.0f, 0.01f);
}

TEST(StyleDefault, GridOffByDefault) {
    auto s = styles::defaultStyle();
    EXPECT_FALSE(s.xAxis.grid);
    EXPECT_FALSE(s.yAxis.grid);
}

TEST(StyleDefault, LineWidth15) {
    auto s = styles::defaultStyle();
    EXPECT_NEAR(s.lines.lineWidth, 1.5f, 0.01f);
}

// ─── classic style ────────────────────────────────────────────────────────
TEST(StyleClassic, LineWidth10) {
    auto s = styles::classicStyle();
    EXPECT_NEAR(s.lines.lineWidth, 1.0f, 0.01f);
}

TEST(StyleClassic, TickDirectionIn) {
    auto s = styles::classicStyle();
    EXPECT_EQ(s.xAxis.ticks.direction, "in");
    EXPECT_EQ(s.yAxis.ticks.direction, "in");
}

TEST(StyleClassic, FontSize12) {
    auto s = styles::classicStyle();
    EXPECT_NEAR(s.fontSize, 12.0f, 0.01f);
}

TEST(StyleClassic, PatchForceEdgeColor) {
    auto s = styles::classicStyle();
    EXPECT_TRUE(s.patch.forceEdgeColor);
}

// ─── ggplot style ─────────────────────────────────────────────────────────
TEST(StyleGgplot, Has7ColorCycle) {
    auto s = styles::ggplotStyle();
    EXPECT_EQ(s.colorCycle.size(), 7u);
}

TEST(StyleGgplot, AxesFaceColorE5E5E5) {
    auto s = styles::ggplotStyle();
    expectColorHex(s.faceColor, "E5E5E5");
}

TEST(StyleGgplot, GridOn) {
    auto s = styles::ggplotStyle();
    EXPECT_TRUE(s.xAxis.grid);
    EXPECT_TRUE(s.yAxis.grid);
}

TEST(StyleGgplot, GridColorWhite) {
    auto s = styles::ggplotStyle();
    EXPECT_NEAR(s.xAxis.gridColor.r, 1.0f, 0.01f);
}

TEST(StyleGgplot, AxisBelowTrue) {
    auto s = styles::ggplotStyle();
    EXPECT_TRUE(s.axisBelow);
}

TEST(StyleGgplot, TickDirectionOut) {
    auto s = styles::ggplotStyle();
    EXPECT_EQ(s.xAxis.ticks.direction, "out");
}

TEST(StyleGgplot, TitleBold) {
    auto s = styles::ggplotStyle();
    EXPECT_EQ(s.title.font.weight, "bold");
}

// ─── seaborn style ────────────────────────────────────────────────────────
TEST(StyleSeaborn, Has6ColorCycle) {
    auto s = styles::seabornStyle();
    EXPECT_EQ(s.colorCycle.size(), 6u);
}

TEST(StyleSeaborn, AxesFaceColorEAEAF2) {
    auto s = styles::seabornStyle();
    expectColorHex(s.faceColor, "EAEAF2");
}

TEST(StyleSeaborn, GridOn) {
    auto s = styles::seabornStyle();
    EXPECT_TRUE(s.xAxis.grid);
    EXPECT_TRUE(s.yAxis.grid);
}

TEST(StyleSeaborn, GridColorWhite) {
    auto s = styles::seabornStyle();
    EXPECT_NEAR(s.xAxis.gridColor.r, 1.0f, 0.01f);
}

TEST(StyleSeaborn, AxisBelowTrue) {
    auto s = styles::seabornStyle();
    EXPECT_TRUE(s.axisBelow);
}

TEST(StyleSeaborn, LegendFrameOff) {
    auto s = styles::seabornStyle();
    EXPECT_FALSE(s.legend.frameOn);
}

TEST(StyleSeaborn, LineCapStyleRound) {
    auto s = styles::seabornStyle();
    EXPECT_EQ(s.lines.solidCapStyle, "round");
}

TEST(StyleSeaborn, TextColorDarkGray) {
    auto s = styles::seabornStyle();
    // .15 → (38, 38, 38)
    EXPECT_NEAR(s.textColor.r, 38/255.0f, 0.02f);
}

TEST(StyleSeaborn, TickMajorSize0) {
    auto s = styles::seabornStyle();
    EXPECT_EQ(s.xAxis.ticks.majorSize, 0);
    EXPECT_EQ(s.yAxis.ticks.majorSize, 0);
}

// ─── seaborn variants ─────────────────────────────────────────────────────
TEST(StyleSeabornDarkgrid, SameAsDefault) {
    auto s1 = styles::seabornStyle();
    auto s2 = styles::seabornDarkgrid();
    EXPECT_EQ(s1.faceColor.r, s2.faceColor.r);
    EXPECT_TRUE(s2.xAxis.grid);
}

TEST(StyleSeabornWhitegrid, WhiteBackground) {
    auto s = styles::seabornWhitegrid();
    EXPECT_NEAR(s.faceColor.r, 1.0f, 0.01f);
    EXPECT_TRUE(s.xAxis.grid);
    // grid color = .8 = (204, 204, 204)
    EXPECT_NEAR(s.xAxis.gridColor.r, 204/255.0f, 0.02f);
}

TEST(StyleSeabornDark, GridOff) {
    auto s = styles::seabornDark();
    EXPECT_FALSE(s.xAxis.grid);
    EXPECT_FALSE(s.yAxis.grid);
    expectColorHex(s.faceColor, "EAEAF2");
}

TEST(StyleSeabornWhite, GridOff) {
    auto s = styles::seabornWhite();
    EXPECT_FALSE(s.xAxis.grid);
    EXPECT_NEAR(s.faceColor.r, 1.0f, 0.01f);
    // edgecolor = .15
    EXPECT_NEAR(s.xAxis.color.r, 38/255.0f, 0.02f);
}

TEST(StyleSeabornTicks, HasTickMarks) {
    auto s = styles::seabornTicks();
    EXPECT_EQ(s.xAxis.ticks.majorSize, 6);
    EXPECT_EQ(s.yAxis.ticks.majorSize, 6);
}

TEST(StyleSeabornPaper, SmallerFonts) {
    auto s = styles::seabornPaper();
    EXPECT_NEAR(s.xAxis.labelFont.size, 11.0f * 0.8f, 0.1f);
}

TEST(StyleSeabornTalk, LargerFonts) {
    auto s = styles::seabornTalk();
    EXPECT_NEAR(s.xAxis.labelFont.size, 11.0f * 1.3f, 0.1f);
}

TEST(StyleSeabornPoster, LargestFonts) {
    auto s = styles::seabornPoster();
    EXPECT_NEAR(s.xAxis.labelFont.size, 11.0f * 1.6f, 0.1f);
}

// ─── seaborn palettes ─────────────────────────────────────────────────────
TEST(StyleSeabornBright, HasBrightPalette) {
    auto s = styles::seabornBright();
    EXPECT_EQ(s.colorCycle.size(), 6u);
    expectColorHex(s.colorCycle.at(0), "003FFF");
}

TEST(StyleSeabornColorblind, HasColorblindPalette) {
    auto s = styles::seabornColorblind();
    EXPECT_EQ(s.colorCycle.size(), 6u);
    expectColorHex(s.colorCycle.at(0), "0072B2");
}

TEST(StyleSeabornDeep, HasDeepPalette) {
    auto s = styles::seabornDeep();
    expectColorHex(s.colorCycle.at(0), "4C72B0");
}

TEST(StyleSeabornMuted, HasMutedPalette) {
    auto s = styles::seabornMuted();
    expectColorHex(s.colorCycle.at(0), "4878CF");
}

TEST(StyleSeabornPastel, HasPastelPalette) {
    auto s = styles::seabornPastel();
    expectColorHex(s.colorCycle.at(0), "92C6FF");
}

// ─── dark_background ──────────────────────────────────────────────────────
TEST(StyleDarkBackground, BlackBackground) {
    auto s = styles::darkBackground();
    EXPECT_NEAR(s.faceColor.r, 0.0f, 0.01f);
    EXPECT_NEAR(s.faceColor.g, 0.0f, 0.01f);
    EXPECT_NEAR(s.faceColor.b, 0.0f, 0.01f);
}

TEST(StyleDarkBackground, WhiteText) {
    auto s = styles::darkBackground();
    EXPECT_NEAR(s.textColor.r, 1.0f, 0.01f);
}

TEST(StyleDarkBackground, WhiteAxes) {
    auto s = styles::darkBackground();
    EXPECT_NEAR(s.xAxis.color.r, 1.0f, 0.01f);
    EXPECT_NEAR(s.yAxis.color.r, 1.0f, 0.01f);
}

TEST(StyleDarkBackground, Has10ColorCycle) {
    auto s = styles::darkBackground();
    EXPECT_EQ(s.colorCycle.size(), 10u);
}

// ─── grayscale ────────────────────────────────────────────────────────────
TEST(StyleGrayscale, Has4ColorCycle) {
    auto s = styles::grayscaleStyle();
    EXPECT_EQ(s.colorCycle.size(), 4u);
}

TEST(StyleGrayscale, FirstColorBlack) {
    auto s = styles::grayscaleStyle();
    EXPECT_NEAR(s.colorCycle.at(0).r, 0.0f, 0.01f);
}

TEST(StyleGrayscale, FaceColor075) {
    auto s = styles::grayscaleStyle();
    // 0.75 → (191, 191, 191)
    EXPECT_NEAR(s.faceColor.r, 191/255.0f, 0.02f);
}

// ─── bmh ──────────────────────────────────────────────────────────────────
TEST(StyleBmh, Has10ColorCycle) {
    auto s = styles::bmhStyle();
    EXPECT_EQ(s.colorCycle.size(), 10u);
}

TEST(StyleBmh, AxesFaceColorEEEEEE) {
    auto s = styles::bmhStyle();
    expectColorHex(s.faceColor, "EEEEEE");
}

TEST(StyleBmh, GridOn) {
    auto s = styles::bmhStyle();
    EXPECT_TRUE(s.xAxis.grid);
    EXPECT_TRUE(s.yAxis.grid);
}

TEST(StyleBmh, GridDashed) {
    auto s = styles::bmhStyle();
    EXPECT_EQ(s.xAxis.gridLineStyle, "--");
}

TEST(StyleBmh, LineWidth2) {
    auto s = styles::bmhStyle();
    EXPECT_NEAR(s.lines.lineWidth, 2.0f, 0.01f);
}

TEST(StyleBmh, TickDirectionIn) {
    auto s = styles::bmhStyle();
    EXPECT_EQ(s.xAxis.ticks.direction, "in");
}

// ─── fivethirtyeight ──────────────────────────────────────────────────────
TEST(StyleFivethirtyeight, Has6ColorCycle) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_EQ(s.colorCycle.size(), 6u);
}

TEST(StyleFivethirtyeight, FaceColorF0F0F0) {
    auto s = styles::fivethirtyeightStyle();
    expectColorHex(s.faceColor, "F0F0F0");
}

TEST(StyleFivethirtyeight, LineWidth4) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_NEAR(s.lines.lineWidth, 4.0f, 0.01f);
}

TEST(StyleFivethirtyeight, LineCapStyleButt) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_EQ(s.lines.solidCapStyle, "butt");
}

TEST(StyleFivethirtyeight, AxisLineWidth3) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_NEAR(s.xAxis.lineWidth, 3.0f, 0.01f);
}

TEST(StyleFivethirtyeight, TickSize0) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_EQ(s.xAxis.ticks.majorSize, 0);
    EXPECT_EQ(s.yAxis.ticks.majorSize, 0);
}

TEST(StyleFivethirtyeight, FontSize14) {
    auto s = styles::fivethirtyeightStyle();
    EXPECT_NEAR(s.fontSize, 14.0f, 0.01f);
}

// ─── Solarize_Light2 ──────────────────────────────────────────────────────
TEST(StyleSolarize, FaceColorFDF6E3) {
    auto s = styles::solarizeLight2Style();
    expectColorHex(s.faceColor, "FDF6E3");
}

TEST(StyleSolarize, Has8ColorCycle) {
    auto s = styles::solarizeLight2Style();
    EXPECT_EQ(s.colorCycle.size(), 8u);
}

TEST(StyleSolarize, TextColor657B83) {
    auto s = styles::solarizeLight2Style();
    expectColorHex(s.textColor, "657B83");
}

TEST(StyleSolarize, GridOn) {
    auto s = styles::solarizeLight2Style();
    EXPECT_TRUE(s.xAxis.grid);
    EXPECT_TRUE(s.yAxis.grid);
}

TEST(StyleSolarize, LineWidth2) {
    auto s = styles::solarizeLight2Style();
    EXPECT_NEAR(s.lines.lineWidth, 2.0f, 0.01f);
}

TEST(StyleSolarize, LineCapStyleButt) {
    auto s = styles::solarizeLight2Style();
    EXPECT_EQ(s.lines.solidCapStyle, "butt");
}

// ─── tableau-colorblind10 ─────────────────────────────────────────────────
TEST(StyleTableauColorblind, Has10ColorCycle) {
    auto s = styles::tableauColorblind10();
    EXPECT_EQ(s.colorCycle.size(), 10u);
    expectColorHex(s.colorCycle.at(0), "006BA4");
}

// ─── petroff6 / petroff8 ──────────────────────────────────────────────────
TEST(StylePetroff6, Has6ColorCycle) {
    auto s = styles::petroff6Style();
    EXPECT_EQ(s.colorCycle.size(), 6u);
    expectColorHex(s.colorCycle.at(0), "5790FC");
}

TEST(StylePetroff8, Has8ColorCycle) {
    auto s = styles::petroff8Style();
    EXPECT_EQ(s.colorCycle.size(), 8u);
    expectColorHex(s.colorCycle.at(0), "1845FB");
}

// ─── byName lookup ────────────────────────────────────────────────────────
TEST(StyleByName, FindsAllStyles) {
    EXPECT_NE(styles::byName("default"), nullptr);
    EXPECT_NE(styles::byName("classic"), nullptr);
    EXPECT_NE(styles::byName("ggplot"), nullptr);
    EXPECT_NE(styles::byName("seaborn"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-darkgrid"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-whitegrid"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-dark"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-white"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-ticks"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-paper"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-notebook"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-talk"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-poster"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-bright"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-colorblind"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-deep"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-muted"), nullptr);
    EXPECT_NE(styles::byName("seaborn-v0_8-pastel"), nullptr);
    EXPECT_NE(styles::byName("dark_background"), nullptr);
    EXPECT_NE(styles::byName("grayscale"), nullptr);
    EXPECT_NE(styles::byName("bmh"), nullptr);
    EXPECT_NE(styles::byName("fivethirtyeight"), nullptr);
    EXPECT_NE(styles::byName("Solarize_Light2"), nullptr);
    EXPECT_NE(styles::byName("fast"), nullptr);
    EXPECT_NE(styles::byName("tableau-colorblind10"), nullptr);
    EXPECT_NE(styles::byName("petroff6"), nullptr);
    EXPECT_NE(styles::byName("petroff8"), nullptr);
}

TEST(StyleByName, ReturnsNullForUnknown) {
    EXPECT_EQ(styles::byName("nonexistent"), nullptr);
}

TEST(StyleByName, ReturnsCorrectStyle) {
    auto fn = styles::byName("dark_background");
    ASSERT_NE(fn, nullptr);
    auto s = fn();
    EXPECT_EQ(s.styleName, "dark_background");
    EXPECT_NEAR(s.faceColor.r, 0.0f, 0.01f);
}

// ─── ColorCycleStyle ──────────────────────────────────────────────────────
TEST(ColorCycleStyle, WrapsAround) {
    ColorCycleStyle cycle;
    cycle.colors = {Color::fromRgba8(255, 0, 0), Color::fromRgba8(0, 255, 0)};
    EXPECT_NEAR(cycle.at(0).r, 1.0f, 0.01f);
    EXPECT_NEAR(cycle.at(1).g, 1.0f, 0.01f);
    EXPECT_NEAR(cycle.at(2).r, 1.0f, 0.01f); // wraps to 0
    EXPECT_NEAR(cycle.at(3).g, 1.0f, 0.01f); // wraps to 1
}

TEST(ColorCycleStyle, EmptyReturnsBlack) {
    ColorCycleStyle cycle;
    EXPECT_NEAR(cycle.at(0).r, 0.0f, 0.01f);
}
