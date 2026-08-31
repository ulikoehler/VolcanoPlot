// volcano/plot/Style.cpp — matplotlib-style presets
// Implements all matplotlib built-in style sheets by translating the
// rcParams from the .mplstyle files into FigureStyle structs.
#include "volcano/plot/Style.hpp"

#include <algorithm>
#include <cstring>

namespace volcano::plot {

namespace {

/// Parse a hex color string (with or without #) into a Color.
Color hex(std::string_view s) {
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    if (s.size() >= 6) {
        int r = hv(s[0]) * 16 + hv(s[1]);
        int g = hv(s[2]) * 16 + hv(s[3]);
        int b = hv(s[4]) * 16 + hv(s[5]);
        int a = (s.size() >= 8) ? hv(s[6]) * 16 + hv(s[7]) : 255;
        return Color::fromRgba8(r, g, b, a);
    }
    if (s.size() >= 3) {
        int r = hv(s[0]) * 17;
        int g = hv(s[1]) * 17;
        int b = hv(s[2]) * 17;
        return Color::fromRgba8(r, g, b);
    }
    return Color::black();
}

/// Grayscale color from a float string like "0.15" → (38,38,38).
Color gray(float v) {
    int c = static_cast<int>(v * 255.0f + 0.5f);
    c = std::clamp(c, 0, 255);
    return Color::fromRgba8(c, c, c);
}

/// matplotlib tab10 palette (the default color cycle).
const std::vector<Color>& tab10() {
    static const std::vector<Color> c = {
        Color::fromRgba8(31, 119, 180),   // C0 blue
        Color::fromRgba8(255, 127, 14),   // C1 orange
        Color::fromRgba8(44, 160, 44),    // C2 green
        Color::fromRgba8(214, 39, 40),    // C3 red
        Color::fromRgba8(148, 103, 189),  // C4 purple
        Color::fromRgba8(140, 86, 75),    // C5 brown
        Color::fromRgba8(227, 119, 194),  // C6 pink
        Color::fromRgba8(127, 127, 127),  // C7 gray
        Color::fromRgba8(188, 189, 34),   // C8 olive
        Color::fromRgba8(23, 190, 207),   // C9 cyan
    };
    return c;
}

/// Set seaborn common parameters (shared by all seaborn variants).
void seabornCommon(FigureStyle& s) {
    s.styleName = "seaborn";
    s.faceColor = Color::white();
    s.textColor = gray(0.15f);
    s.axisBelow = true;
    s.fontFamily = "sans-serif";
    s.fontSize = 11.0f;
    s.xAxis.labelColor = gray(0.15f);
    s.yAxis.labelColor = gray(0.15f);
    s.xAxis.color = gray(0.15f);
    s.yAxis.color = gray(0.15f);
    s.xAxis.ticks.direction = "out";
    s.yAxis.ticks.direction = "out";
    s.xAxis.ticks.majorSize = 0;
    s.yAxis.ticks.majorSize = 0;
    s.xAxis.ticks.minorSize = 0;
    s.yAxis.ticks.minorSize = 0;
    s.legend.frameOn = false;
    s.lines.solidCapStyle = "round";
    s.lines.lineWidth = 1.75f;
    s.patch.lineWidth = 0.3f;
    s.patch.faceColor = hex("4C72B0");
}

/// Apply seaborn context scaling (paper/notebook/talk/poster).
void seabornContext(FigureStyle& s, float scale) {
    s.xAxis.labelFont.size = 11.0f * scale;
    s.yAxis.labelFont.size = 11.0f * scale;
    s.title.font.size = 12.0f * scale;
    s.xAxis.tickFont.size = 10.0f * scale;
    s.yAxis.tickFont.size = 10.0f * scale;
    s.legend.font.size = 10.0f * scale;
    s.xAxis.gridLineWidth = 1.0f * scale;
    s.yAxis.gridLineWidth = 1.0f * scale;
    s.lines.lineWidth = 1.75f * scale;
    s.patch.lineWidth = 0.3f * scale;
    s.xAxis.ticks.majorWidth = 1.0f * scale;
    s.yAxis.ticks.majorWidth = 1.0f * scale;
    s.xAxis.ticks.minorWidth = 0.5f * scale;
    s.yAxis.ticks.minorWidth = 0.5f * scale;
}

} // namespace

namespace styles {

// ─── default ──────────────────────────────────────────────────────────────
FigureStyle defaultStyle() {
    FigureStyle s;
    s.styleName = "default";
    s.faceColor = Color::white();
    s.edgeColor = Color::white();
    s.textColor = Color::black();
    s.axisBelow = false;
    s.fontSize = 10.0f;
    s.fontFamily = "sans-serif";
    s.colorCycle.colors = tab10();
    s.xAxis.color = Color::black();
    s.yAxis.color = Color::black();
    s.xAxis.lineWidth = 0.8f;
    s.yAxis.lineWidth = 0.8f;
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.xAxis.gridColor = Color::fromRgba8(176, 176, 176);
    s.yAxis.gridColor = Color::fromRgba8(176, 176, 176);
    s.xAxis.gridLineWidth = 0.8f;
    s.yAxis.gridLineWidth = 0.8f;
    s.xAxis.labelColor = Color::black();
    s.yAxis.labelColor = Color::black();
    s.xAxis.ticks.direction = "out";
    s.yAxis.ticks.direction = "out";
    s.lines.lineWidth = 1.5f;
    s.lines.solidCapStyle = "projecting";
    s.lines.dashCapStyle = "butt";
    s.patch.faceColor = hex("1F77B4"); // tab:blue
    s.patch.edgeColor = Color::black();
    s.patch.lineWidth = 1.0f;
    s.legend.frameOn = true;
    s.legend.faceColor = Color::fromRgba8(255, 255, 255, 200);
    s.legend.edgeColor = Color::black();
    return s;
}

// ─── classic ──────────────────────────────────────────────────────────────
FigureStyle classicStyle() {
    FigureStyle s;
    s.styleName = "classic";
    s.faceColor = Color::white();
    s.edgeColor = Color::white();
    s.textColor = Color::black();
    s.axisBelow = false;
    s.fontSize = 12.0f;
    s.fontFamily = "sans-serif";
    // classic uses the same tab10 cycle
    s.colorCycle.colors = tab10();
    s.xAxis.color = Color::black();
    s.yAxis.color = Color::black();
    s.xAxis.lineWidth = 1.0f;
    s.yAxis.lineWidth = 1.0f;
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.xAxis.gridColor = Color::black();
    s.yAxis.gridColor = Color::black();
    s.xAxis.gridLineWidth = 0.5f;
    s.yAxis.gridLineWidth = 0.5f;
    s.xAxis.labelColor = Color::black();
    s.yAxis.labelColor = Color::black();
    s.xAxis.ticks.direction = "in";
    s.yAxis.ticks.direction = "in";
    s.lines.lineWidth = 1.0f;
    s.lines.solidCapStyle = "projecting";
    s.lines.dashCapStyle = "butt";
    s.lines.solidJoinStyle = "round";
    s.lines.dashJoinStyle = "round";
    s.patch.faceColor = hex("0000FF"); // blue
    s.patch.edgeColor = Color::black();
    s.patch.lineWidth = 1.0f;
    s.patch.forceEdgeColor = true;
    s.legend.frameOn = true;
    return s;
}

// ─── ggplot ───────────────────────────────────────────────────────────────
FigureStyle ggplotStyle() {
    FigureStyle s;
    s.styleName = "ggplot";
    s.faceColor = Color::white();
    s.edgeColor = gray(0.50f);
    s.textColor = Color::black();
    s.axisBelow = true;
    s.fontSize = 10.0f;
    s.fontFamily = "sans-serif";
    s.colorCycle.colors = {
        hex("E24A33"), hex("348ABD"), hex("988ED5"), hex("777777"),
        hex("FBC15E"), hex("8EBA42"), hex("FFB5B8"),
    };
    s.faceColor = hex("E5E5E5"); // axes.facecolor
    s.xAxis.color = Color::white();   // axes.edgecolor
    s.yAxis.color = Color::white();
    s.xAxis.lineWidth = 1.0f;
    s.yAxis.lineWidth = 1.0f;
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = Color::white();
    s.yAxis.gridColor = Color::white();
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.xAxis.gridLineStyle = "-";
    s.yAxis.gridLineStyle = "-";
    s.xAxis.labelColor = hex("555555");
    s.yAxis.labelColor = hex("555555");
    s.xAxis.ticks.direction = "out";
    s.yAxis.ticks.direction = "out";
    s.title.font.size = 14.0f; // x-large
    s.title.font.weight = "bold";
    s.lines.lineWidth = 1.5f;
    s.patch.faceColor = hex("348ABD");
    s.patch.edgeColor = hex("EEEEEE");
    s.patch.lineWidth = 0.5f;
    s.legend.frameOn = true;
    return s;
}

// ─── seaborn variants ─────────────────────────────────────────────────────

FigureStyle seabornStyle() {
    // default seaborn = darkgrid + deep palette + notebook context
    FigureStyle s;
    seabornCommon(s);
    s.styleName = "seaborn-v0_8";
    // darkgrid
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = Color::white();
    s.yAxis.gridColor = Color::white();
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.xAxis.gridLineStyle = "-";
    s.yAxis.gridLineStyle = "-";
    // axes facecolor = EAEAF2 (light gray-blue)
    // We represent axes facecolor via xAxis.faceColor but the renderer uses
    // figure faceColor for the axes background. Set it there.
    s.faceColor = hex("EAEAF2");
    s.xAxis.color = Color::white();
    s.yAxis.color = Color::white();
    s.xAxis.lineWidth = 0.0f;
    s.yAxis.lineWidth = 0.0f;
    // deep palette
    s.colorCycle.colors = {
        hex("4C72B0"), hex("55A868"), hex("C44E52"), hex("8172B2"),
        hex("CCB974"), hex("64B5CD"),
    };
    seabornContext(s, 1.0f); // notebook
    return s;
}

FigureStyle seabornDarkgrid() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-darkgrid";
    return s;
}

FigureStyle seabornWhitegrid() {
    FigureStyle s;
    seabornCommon(s);
    s.styleName = "seaborn-v0_8-whitegrid";
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.faceColor = Color::white();
    s.xAxis.color = gray(0.8f);
    s.yAxis.color = gray(0.8f);
    s.xAxis.lineWidth = 1.0f;
    s.yAxis.lineWidth = 1.0f;
    s.xAxis.gridColor = gray(0.8f);
    s.yAxis.gridColor = gray(0.8f);
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.colorCycle.colors = {
        hex("4C72B0"), hex("55A868"), hex("C44E52"), hex("8172B2"),
        hex("CCB974"), hex("64B5CD"),
    };
    seabornContext(s, 1.0f);
    return s;
}

FigureStyle seabornDark() {
    FigureStyle s;
    seabornCommon(s);
    s.styleName = "seaborn-v0_8-dark";
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.faceColor = hex("EAEAF2");
    s.xAxis.color = Color::white();
    s.yAxis.color = Color::white();
    s.xAxis.lineWidth = 0.0f;
    s.yAxis.lineWidth = 0.0f;
    s.colorCycle.colors = {
        hex("4C72B0"), hex("55A868"), hex("C44E52"), hex("8172B2"),
        hex("CCB974"), hex("64B5CD"),
    };
    seabornContext(s, 1.0f);
    return s;
}

FigureStyle seabornWhite() {
    FigureStyle s;
    seabornCommon(s);
    s.styleName = "seaborn-v0_8-white";
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.faceColor = Color::white();
    s.xAxis.color = gray(0.15f);
    s.yAxis.color = gray(0.15f);
    s.xAxis.lineWidth = 1.25f;
    s.yAxis.lineWidth = 1.25f;
    s.colorCycle.colors = {
        hex("4C72B0"), hex("55A868"), hex("C44E52"), hex("8172B2"),
        hex("CCB974"), hex("64B5CD"),
    };
    seabornContext(s, 1.0f);
    return s;
}

FigureStyle seabornTicks() {
    FigureStyle s = seabornWhite();
    s.styleName = "seaborn-v0_8-ticks";
    s.xAxis.ticks.majorSize = 6;
    s.yAxis.ticks.majorSize = 6;
    s.xAxis.ticks.minorSize = 3;
    s.yAxis.ticks.minorSize = 3;
    return s;
}

FigureStyle seabornPaper() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-paper";
    seabornContext(s, 0.8f);
    return s;
}

FigureStyle seabornNotebook() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-notebook";
    seabornContext(s, 1.0f);
    return s;
}

FigureStyle seabornTalk() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-talk";
    seabornContext(s, 1.3f);
    return s;
}

FigureStyle seabornPoster() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-poster";
    seabornContext(s, 1.6f);
    return s;
}

// ─── seaborn palettes (color-only overrides) ──────────────────────────────

FigureStyle seabornBright() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-bright";
    s.colorCycle.colors = {
        hex("003FFF"), hex("03ED3A"), hex("E8000B"), hex("8A2BE2"),
        hex("FFC400"), hex("00D7FF"),
    };
    s.patch.faceColor = hex("003FFF");
    return s;
}

FigureStyle seabornColorblind() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-colorblind";
    s.colorCycle.colors = {
        hex("0072B2"), hex("009E73"), hex("D55E00"), hex("CC79A7"),
        hex("F0E442"), hex("56B4E9"),
    };
    s.patch.faceColor = hex("0072B2");
    return s;
}

FigureStyle seabornDeep() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-deep";
    s.colorCycle.colors = {
        hex("4C72B0"), hex("55A868"), hex("C44E52"), hex("8172B2"),
        hex("CCB974"), hex("64B5CD"),
    };
    s.patch.faceColor = hex("4C72B0");
    return s;
}

FigureStyle seabornMuted() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-muted";
    s.colorCycle.colors = {
        hex("4878CF"), hex("6ACC65"), hex("D65F5F"), hex("B47CC7"),
        hex("C4AD66"), hex("77BEDB"),
    };
    s.patch.faceColor = hex("4878CF");
    return s;
}

FigureStyle seabornPastel() {
    FigureStyle s = seabornStyle();
    s.styleName = "seaborn-v0_8-pastel";
    s.colorCycle.colors = {
        hex("92C6FF"), hex("97F0AA"), hex("FF9F9A"), hex("D0BBFF"),
        hex("FFFEA3"), hex("B0E0E6"),
    };
    s.patch.faceColor = hex("92C6FF");
    return s;
}

// ─── dark_background ──────────────────────────────────────────────────────
FigureStyle darkBackground() {
    FigureStyle s;
    s.styleName = "dark_background";
    s.faceColor = Color::black();
    s.edgeColor = Color::black();
    s.textColor = Color::white();
    s.axisBelow = false;
    s.colorCycle.colors = {
        hex("8dd3c7"), hex("feffb3"), hex("bfbbd9"), hex("fa8174"),
        hex("81b1d2"), hex("fdb462"), hex("b3de69"), hex("bc82bd"),
        hex("ccebc4"), hex("ffed6f"),
    };
    s.xAxis.color = Color::white();
    s.yAxis.color = Color::white();
    s.xAxis.lineWidth = 0.8f;
    s.yAxis.lineWidth = 0.8f;
    s.xAxis.grid = false;
    s.yAxis.grid = false;
    s.xAxis.gridColor = Color::white();
    s.yAxis.gridColor = Color::white();
    s.xAxis.labelColor = Color::white();
    s.yAxis.labelColor = Color::white();
    s.xAxis.ticks.direction = "out";
    s.yAxis.ticks.direction = "out";
    s.title.color = Color::white();
    s.lines.lineWidth = 1.5f;
    s.lines.solidCapStyle = "projecting";
    s.patch.faceColor = hex("8dd3c7");
    s.patch.edgeColor = Color::white();
    s.legend.faceColor = Color::fromRgba8(40, 40, 40, 200);
    s.legend.edgeColor = Color::white();
    s.legend.frameOn = true;
    return s;
}

// ─── grayscale ────────────────────────────────────────────────────────────
FigureStyle grayscaleStyle() {
    FigureStyle s = defaultStyle();
    s.styleName = "grayscale";
    s.faceColor = gray(0.75f);
    s.edgeColor = Color::white();
    s.textColor = Color::black();
    s.colorCycle.colors = {
        gray(0.00f), gray(0.40f), gray(0.60f), gray(0.70f),
    };
    s.xAxis.color = Color::black();
    s.yAxis.color = Color::black();
    s.xAxis.gridColor = Color::black();
    s.yAxis.gridColor = Color::black();
    s.xAxis.labelColor = Color::black();
    s.yAxis.labelColor = Color::black();
    s.patch.faceColor = gray(0.5f);
    s.patch.edgeColor = Color::black();
    return s;
}

// ─── bmh ──────────────────────────────────────────────────────────────────
FigureStyle bmhStyle() {
    FigureStyle s;
    s.styleName = "bmh";
    s.faceColor = hex("EEEEEE");
    s.edgeColor = Color::white();
    s.textColor = Color::black();
    s.axisBelow = false;
    s.colorCycle.colors = {
        hex("348ABD"), hex("A60628"), hex("7A68A6"), hex("467821"),
        hex("D55E00"), hex("CC79A7"), hex("56B4E9"), hex("009E73"),
        hex("F0E442"), hex("0072B2"),
    };
    s.xAxis.color = hex("BCBCBC");
    s.yAxis.color = hex("BCBCBC");
    s.xAxis.lineWidth = 0.8f;
    s.yAxis.lineWidth = 0.8f;
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = hex("B2B2B2");
    s.yAxis.gridColor = hex("B2B2B2");
    s.xAxis.gridLineWidth = 0.5f;
    s.yAxis.gridLineWidth = 0.5f;
    s.xAxis.gridLineStyle = "--";
    s.yAxis.gridLineStyle = "--";
    s.xAxis.ticks.direction = "in";
    s.yAxis.ticks.direction = "in";
    s.lines.lineWidth = 2.0f;
    s.patch.faceColor = hex("348ABD");
    s.patch.edgeColor = hex("EEEEEE");
    s.patch.lineWidth = 0.5f;
    s.legend.frameOn = true;
    s.title.font.size = 16.0f; // x-large
    return s;
}

// ─── fivethirtyeight ──────────────────────────────────────────────────────
FigureStyle fivethirtyeightStyle() {
    FigureStyle s;
    s.styleName = "fivethirtyeight";
    s.faceColor = hex("F0F0F0");
    s.edgeColor = hex("F0F0F0");
    s.textColor = Color::black();
    s.axisBelow = true;
    s.fontSize = 14.0f;
    s.colorCycle.colors = {
        hex("008fd5"), hex("fc4f30"), hex("e5ae38"), hex("6d904f"),
        hex("8b8b8b"), hex("810f7c"),
    };
    s.xAxis.color = hex("F0F0F0");
    s.yAxis.color = hex("F0F0F0");
    s.xAxis.lineWidth = 3.0f;
    s.yAxis.lineWidth = 3.0f;
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = hex("CBCBCB");
    s.yAxis.gridColor = hex("CBCBCB");
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.xAxis.gridLineStyle = "-";
    s.yAxis.gridLineStyle = "-";
    s.xAxis.ticks.majorSize = 0;
    s.yAxis.ticks.majorSize = 0;
    s.xAxis.ticks.minorSize = 0;
    s.yAxis.ticks.minorSize = 0;
    s.lines.lineWidth = 4.0f;
    s.lines.solidCapStyle = "butt";
    s.patch.edgeColor = hex("F0F0F0");
    s.patch.lineWidth = 0.5f;
    s.legend.frameOn = true;
    s.title.font.size = 16.0f; // x-large
    return s;
}

// ─── Solarize_Light2 ──────────────────────────────────────────────────────
FigureStyle solarizeLight2Style() {
    FigureStyle s;
    s.styleName = "Solarize_Light2";
    s.faceColor = hex("FDF6E3");
    s.edgeColor = hex("FDF6E3");
    s.textColor = hex("657B83");
    s.axisBelow = true;
    s.colorCycle.colors = {
        hex("268BD2"), hex("2AA198"), hex("859900"), hex("B58900"),
        hex("CB4B16"), hex("DC322F"), hex("D33682"), hex("6C71C4"),
    };
    s.xAxis.color = hex("EEE8D5");
    s.yAxis.color = hex("EEE8D5");
    s.xAxis.lineWidth = 0.8f;
    s.yAxis.lineWidth = 0.8f;
    s.xAxis.grid = true;
    s.yAxis.grid = true;
    s.xAxis.gridColor = hex("FDF6E3");
    s.yAxis.gridColor = hex("FDF6E3");
    s.xAxis.gridLineWidth = 1.0f;
    s.yAxis.gridLineWidth = 1.0f;
    s.xAxis.gridLineStyle = "-";
    s.yAxis.gridLineStyle = "-";
    s.xAxis.labelColor = hex("657B83");
    s.yAxis.labelColor = hex("657B83");
    s.xAxis.ticks.direction = "out";
    s.yAxis.ticks.direction = "out";
    s.title.font.size = 16.0f;
    s.title.color = hex("657B83");
    s.lines.lineWidth = 2.0f;
    s.lines.solidCapStyle = "butt";
    s.patch.faceColor = hex("268BD2");
    s.patch.edgeColor = hex("EEE8D5");
    return s;
}

// ─── fast ─────────────────────────────────────────────────────────────────
FigureStyle fastStyle() {
    // "fast" only changes path simplification settings, which are rendering
    // optimizations not style parameters. Return default with the name set.
    FigureStyle s = defaultStyle();
    s.styleName = "fast";
    return s;
}

// ─── tableau-colorblind10 ─────────────────────────────────────────────────
FigureStyle tableauColorblind10() {
    FigureStyle s = defaultStyle();
    s.styleName = "tableau-colorblind10";
    s.colorCycle.colors = {
        hex("006BA4"), hex("FF800E"), hex("ABABAB"), hex("595959"),
        hex("5F9ED1"), hex("C85200"), hex("898989"), hex("A2C8EC"),
        hex("FFBC79"), hex("CFCFCF"),
    };
    s.patch.faceColor = hex("006BA4");
    return s;
}

// ─── petroff6 ─────────────────────────────────────────────────────────────
FigureStyle petroff6Style() {
    FigureStyle s = defaultStyle();
    s.styleName = "petroff6";
    s.colorCycle.colors = {
        hex("5790fc"), hex("f89c20"), hex("e42536"), hex("964a8b"),
        hex("9c9ca1"), hex("7a21dd"),
    };
    s.patch.faceColor = hex("5790fc");
    return s;
}

// ─── petroff8 ─────────────────────────────────────────────────────────────
FigureStyle petroff8Style() {
    FigureStyle s = defaultStyle();
    s.styleName = "petroff8";
    s.colorCycle.colors = {
        hex("1845fb"), hex("ff5e02"), hex("c91f16"), hex("c849a9"),
        hex("adad7d"), hex("86c8dd"), hex("578dff"), hex("656364"),
    };
    s.patch.faceColor = hex("1845fb");
    return s;
}

// ─── byName ───────────────────────────────────────────────────────────────
FigureStyle (*byName(const std::string& name))() {
    if (name == "default") return defaultStyle;
    if (name == "classic") return classicStyle;
    if (name == "ggplot") return ggplotStyle;
    if (name == "seaborn" || name == "seaborn-v0_8") return seabornStyle;
    if (name == "seaborn-v0_8-darkgrid" || name == "seaborn-darkgrid") return seabornDarkgrid;
    if (name == "seaborn-v0_8-whitegrid" || name == "seaborn-whitegrid") return seabornWhitegrid;
    if (name == "seaborn-v0_8-dark" || name == "seaborn-dark") return seabornDark;
    if (name == "seaborn-v0_8-white" || name == "seaborn-white") return seabornWhite;
    if (name == "seaborn-v0_8-ticks" || name == "seaborn-ticks") return seabornTicks;
    if (name == "seaborn-v0_8-paper" || name == "seaborn-paper") return seabornPaper;
    if (name == "seaborn-v0_8-notebook" || name == "seaborn-notebook") return seabornNotebook;
    if (name == "seaborn-v0_8-talk" || name == "seaborn-talk") return seabornTalk;
    if (name == "seaborn-v0_8-poster" || name == "seaborn-poster") return seabornPoster;
    if (name == "seaborn-v0_8-bright" || name == "seaborn-bright") return seabornBright;
    if (name == "seaborn-v0_8-colorblind" || name == "seaborn-colorblind") return seabornColorblind;
    if (name == "seaborn-v0_8-deep" || name == "seaborn-deep") return seabornDeep;
    if (name == "seaborn-v0_8-muted" || name == "seaborn-muted") return seabornMuted;
    if (name == "seaborn-v0_8-pastel" || name == "seaborn-pastel") return seabornPastel;
    if (name == "dark_background") return darkBackground;
    if (name == "grayscale") return grayscaleStyle;
    if (name == "bmh") return bmhStyle;
    if (name == "fivethirtyeight") return fivethirtyeightStyle;
    if (name == "Solarize_Light2") return solarizeLight2Style;
    if (name == "fast") return fastStyle;
    if (name == "tableau-colorblind10") return tableauColorblind10;
    if (name == "petroff6") return petroff6Style;
    if (name == "petroff8") return petroff8Style;
    return nullptr;
}

} // namespace styles

} // namespace volcano::plot
