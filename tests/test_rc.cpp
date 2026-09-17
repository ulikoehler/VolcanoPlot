// tests/test_rc.cpp — tests for rcParams / style sheet system
#include <gtest/gtest.h>
#include <volcano/plot/Rc.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Types.hpp>

#include <cstdio>
#include <fstream>

using namespace volcano::plot;

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

// Restore global params after each test.
struct RcGuard {
    ~RcGuard() { rc::rcdefaults(); }
};

// ─── rc::set / rc::params ─────────────────────────────────────────────────

TEST(RcParams, SetColorParam) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.edgecolor", "red"));
    expectColorHex(rc::params().xAxis.color, "FF0000");
    expectColorHex(rc::params().yAxis.color, "FF0000");
}

TEST(RcParams, SetFloatParam) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("lines.linewidth", "3.5"));
    EXPECT_FLOAT_EQ(rc::params().lines.lineWidth, 3.5f);
}

TEST(RcParams, SetBoolParam) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.grid", "True"));
    EXPECT_TRUE(rc::params().xAxis.grid);
    EXPECT_TRUE(rc::params().yAxis.grid);
}

TEST(RcParams, UnknownKeyFails) {
    RcGuard guard;
    EXPECT_FALSE(rc::set("totally.bogus.key", "1"));
}

TEST(RcParams, PropCycle) {
    RcGuard guard;
    EXPECT_TRUE(rc::set("axes.prop_cycle",
                        "cycler('color', ['3f90da', 'ffa90e', '#bd1f01'])"));
    const auto& colors = rc::params().colorCycle.colors;
    ASSERT_EQ(colors.size(), 3u);
    expectColorHex(colors[0], "3F90DA");
    expectColorHex(colors[1], "FFA90E");
    expectColorHex(colors[2], "BD1F01");
}

TEST(RcParams, NamedFontSizes) {
    RcGuard guard;
    rc::params().fontSize = 10.0f;
    EXPECT_TRUE(rc::set("axes.titlesize", "x-large"));
    EXPECT_FLOAT_EQ(rc::params().title.font.size, 14.4f);
}

TEST(RcParams, AxesSnapshotParams) {
    RcGuard guard;
    rc::set("axes.edgecolor", "#00ff00");
    Axes a;
    expectColorHex(a.style().xAxis.color, "00FF00");
}

// ─── rc::Context ──────────────────────────────────────────────────────────

TEST(RcContext, RestoresOnScopeExit) {
    RcGuard guard;
    auto before = rc::params().faceColor;
    {
        rc::Context ctx;
        rc::set("figure.facecolor", "black");
        EXPECT_NEAR(rc::params().faceColor.r, 0.0f, 0.01f);
    }
    EXPECT_NEAR(rc::params().faceColor.r, before.r, 0.001f);
}

TEST(RcContext, TakesStyleOverride) {
    RcGuard guard;
    {
        rc::Context ctx(styles::darkBackground());
        EXPECT_NEAR(rc::params().faceColor.r, 0.0f, 0.01f);
        EXPECT_NEAR(rc::params().textColor.r, 1.0f, 0.01f);
    }
    EXPECT_NEAR(rc::params().faceColor.r, 1.0f, 0.01f);
}

// ─── rcdefaults ───────────────────────────────────────────────────────────

TEST(RcDefaults, ResetsToDefault) {
    RcGuard guard;
    rc::set("lines.linewidth", "9.9");
    rc::rcdefaults();
    EXPECT_FLOAT_EQ(rc::params().lines.lineWidth, 1.5f);
}

// ─── file loading ─────────────────────────────────────────────────────────

TEST(RcFile, LoadMplstyleFile) {
    RcGuard guard;
    const char* path = "/tmp/volcano_test_style.mplstyle";
    {
        std::ofstream f(path);
        f << "# custom style\n"
          << "axes.facecolor: EAEAF2\n"
          << "axes.grid: True\n"
          << "lines.linewidth: 2.5   # thick lines\n"
          << "axes.prop_cycle: cycler('color', ['3f90da', 'ffa90e'])\n"
          << "grid.linestyle: --\n";
    }
    ASSERT_TRUE(rc::loadFile(path));
    std::remove(path);

    expectColorHex(rc::params().faceColor, "EAEAF2");
    EXPECT_TRUE(rc::params().xAxis.grid);
    EXPECT_FLOAT_EQ(rc::params().lines.lineWidth, 2.5f);
    ASSERT_EQ(rc::params().colorCycle.colors.size(), 2u);
    expectColorHex(rc::params().colorCycle.colors[0], "3F90DA");
    EXPECT_EQ(rc::params().xAxis.gridLineStyle, "--");
}

TEST(RcFile, MissingFileFails) {
    RcGuard guard;
    EXPECT_FALSE(rc::loadFile("/nonexistent/path/style.mplstyle"));
}

// ─── style::use / context / available ─────────────────────────────────────

TEST(StyleUse, BuiltinByName) {
    RcGuard guard;
    ASSERT_TRUE(style::use("ggplot"));
    EXPECT_EQ(rc::params().styleName, "ggplot");
    EXPECT_TRUE(rc::params().xAxis.grid);
}

TEST(StyleUse, UnknownNameFails) {
    RcGuard guard;
    EXPECT_FALSE(style::use("nonexistent-style-name"));
}

TEST(StyleUse, ComposableList) {
    RcGuard guard;
    const char* path = "/tmp/volcano_test_overlay.mplstyle";
    {
        std::ofstream f(path);
        f << "lines.linewidth: 7.7\n";
    }
    ASSERT_TRUE(style::use({"ggplot", path}));
    std::remove(path);

    EXPECT_EQ(rc::params().styleName, "ggplot");
    EXPECT_FLOAT_EQ(rc::params().lines.lineWidth, 7.7f);
}

TEST(StyleUse, ContextRestores) {
    RcGuard guard;
    {
        auto ctx = style::context("dark_background");
        EXPECT_EQ(rc::params().styleName, "dark_background");
    }
    EXPECT_EQ(rc::params().styleName, "default");
}

TEST(StyleUse, AvailableListsBuiltins) {
    const auto& names = style::available();
    EXPECT_NE(std::find(names.begin(), names.end(), "ggplot"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "petroff10"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "xkcd"), names.end());
    // Every name in available() must resolve via byName.
    for (const auto& n : names) {
        EXPECT_NE(styles::byName(n), nullptr) << "available() lists " << n
                                            << " but byName returns nullptr";
    }
}

// ─── new builtin styles ───────────────────────────────────────────────────

TEST(StylePetroff10, HasTenColors) {
    auto s = styles::petroff10Style();
    ASSERT_EQ(s.colorCycle.size(), 10u);
    expectColorHex(s.colorCycle.at(0), "3F90DA");
    expectColorHex(s.colorCycle.at(1), "FFA90E");
}

TEST(StyleXkcd, ComicSansFont) {
    auto s = styles::xkcdStyle();
    EXPECT_EQ(s.styleName, "xkcd");
    EXPECT_NE(s.fontFamily.find("Comic"), std::string::npos);
    EXPECT_GT(s.xAxis.lineWidth, 1.0f);
}
