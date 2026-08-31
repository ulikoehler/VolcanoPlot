// volcano/plot/Style.hpp — matplotlib-style plot styling
#pragma once

#include "volcano/plot/Types.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace volcano::plot {

/// Font description (matplotlib font_properties equivalent).
struct FontProperties {
    std::string family = "DejaVu Sans";
    std::string style = "normal";   // normal, italic, oblique
    std::string weight = "normal";  // normal, bold, light
    float size = 12.0f;             // points
};

/// Tick configuration for one axis.
struct TickConfig {
    /// Number of major ticks to aim for (matplotlib 'MaxNLocator' style).
    /// matplotlib's AutoLocator defaults to nbins=9.
    int nbins = 9;
    /// If set, fixed tick positions.
    std::optional<std::vector<float>> positions;
    /// If set, fixed tick labels (parallel to positions).
    std::optional<std::vector<std::string>> labels;
    /// Whether to show minor ticks.
    bool minor = true;
    /// Label format, e.g. "%.2f" or "%.1e".
    std::string format = "%g";
    /// Tick direction: "in", "out", or "inout".
    std::string direction = "out";
    /// Major tick size in points.
    float majorSize = 3.5f;
    /// Minor tick size in points.
    float minorSize = 2.0f;
    /// Major tick width in points.
    float majorWidth = 0.8f;
    /// Minor tick width in points.
    float minorWidth = 0.6f;
};

/// Axis appearance configuration.
struct AxisStyle {
    bool visible = true;
    Color color = Color::black();
    float lineWidth = 0.8f;
    std::string label;       // axis label text
    FontProperties labelFont;
    FontProperties tickFont;
    TickConfig ticks;
    /// Grid lines on this axis.
    bool grid = false;
    Color gridColor = Color::fromRgba8(176, 176, 176);
    float gridLineWidth = 0.8f;
    /// Grid line style: "-", "--", ":", "-."
    std::string gridLineStyle = "-";
    /// Log scale.
    bool logScale = false;
    /// Label color (defaults to axis color if not set).
    Color labelColor = Color::black();
};

/// Legend configuration.
struct LegendStyle {
    bool visible = false;
    std::string location = "best"; // best, upper right, upper left, lower right, ...
    FontProperties font;
    Color faceColor = Color::fromRgba8(255, 255, 255, 200);
    Color edgeColor = Color::black();
    float frameAlpha = 0.8f;
    /// Whether to draw a frame around the legend.
    bool frameOn = true;
};

/// Colorbar configuration.
struct ColorbarStyle {
    bool visible = false;
    std::string colormap = "viridis";
    Color edgeColor = Color::black();
    float width = 20.0f;       // pixel width of the color strip
    float padding = 10.0f;     // padding from the axes rect
    FontProperties labelFont;
    Color labelColor = Color::black();
};

/// Title configuration.
struct TitleStyle {
    std::string text;
    FontProperties font;
    Color color = Color::black();
    float pad = 6.0f;
    /// Font weight for the title (e.g. "normal", "bold").
    std::string weight = "normal";
};

/// Line style defaults (matplotlib lines.* rcParams).
struct LineStyleDefaults {
    float lineWidth = 1.5f;
    /// Solid cap style: "butt", "round", "projecting"
    std::string solidCapStyle = "projecting";
    /// Dash cap style: "butt", "round", "projecting"
    std::string dashCapStyle = "butt";
    /// Solid join style: "miter", "round", "bevel"
    std::string solidJoinStyle = "round";
    /// Dash join style: "miter", "round", "bevel"
    std::string dashJoinStyle = "round";
};

/// Patch style defaults (matplotlib patch.* rcParams).
struct PatchStyleDefaults {
    float lineWidth = 1.0f;
    Color faceColor = Color::fromRgba8(31, 119, 180); // tab:blue = C0
    Color edgeColor = Color::black();
    bool forceEdgeColor = false;
};

/// A color cycle (matplotlib axes.prop_cycle equivalent).
struct ColorCycleStyle {
    std::vector<Color> colors;
    [[nodiscard]] Color at(size_t i) const {
        if (colors.empty()) return Color::black();
        return colors[i % colors.size()];
    }
    [[nodiscard]] size_t size() const { return colors.size(); }
};

/// Overall figure style (matplotlib rcParams subset).
struct FigureStyle {
    Color faceColor = Color::white();
    Color edgeColor = Color::white();
    float dpi = 100.0f;
    std::string styleName = "default"; // ggplot, seaborn, default, ...
    TitleStyle title;
    LegendStyle legend;
    ColorbarStyle colorbar;
    AxisStyle xAxis;
    AxisStyle yAxis;
    AxisStyle zAxis; // for 3D

    /// Text color (matplotlib text.color).
    Color textColor = Color::black();

    /// Whether grid/ticks are below plot elements (axes.axisbelow).
    /// "line" = default (grid below, ticks above), true = grid+ticks below, false = above.
    bool axisBelow = false;

    /// Color cycle for automatic plot coloring (axes.prop_cycle).
    ColorCycleStyle colorCycle;

    /// Line style defaults.
    LineStyleDefaults lines;

    /// Patch style defaults.
    PatchStyleDefaults patch;

    /// Font family override (font.family).
    std::string fontFamily = "sans-serif";

    /// Default font size (font.size).
    float fontSize = 10.0f;
};

/// Built-in style presets (matplotlib style sheets equivalent).
namespace styles {
    /// matplotlib "default" style (modern matplotlib defaults).
    FigureStyle defaultStyle();
    /// matplotlib "classic" style (pre-2.0 defaults).
    FigureStyle classicStyle();
    /// ggplot style.
    FigureStyle ggplotStyle();
    /// seaborn-v0_8 default (darkgrid + deep palette + notebook context).
    FigureStyle seabornStyle();
    /// seaborn-v0_8-darkgrid.
    FigureStyle seabornDarkgrid();
    /// seaborn-v0_8-whitegrid.
    FigureStyle seabornWhitegrid();
    /// seaborn-v0_8-dark.
    FigureStyle seabornDark();
    /// seaborn-v0_8-white.
    FigureStyle seabornWhite();
    /// seaborn-v0_8-ticks.
    FigureStyle seabornTicks();
    /// seaborn-v0_8-paper context (smaller fonts).
    FigureStyle seabornPaper();
    /// seaborn-v0_8-notebook context (medium fonts, default).
    FigureStyle seabornNotebook();
    /// seaborn-v0_8-talk context (larger fonts).
    FigureStyle seabornTalk();
    /// seaborn-v0_8-poster context (largest fonts).
    FigureStyle seabornPoster();
    /// seaborn-v0_8-bright palette.
    FigureStyle seabornBright();
    /// seaborn-v0_8-colorblind palette.
    FigureStyle seabornColorblind();
    /// seaborn-v0_8-deep palette.
    FigureStyle seabornDeep();
    /// seaborn-v0_8-muted palette.
    FigureStyle seabornMuted();
    /// seaborn-v0_8-pastel palette.
    FigureStyle seabornPastel();
    /// dark_background style.
    FigureStyle darkBackground();
    /// grayscale style.
    FigureStyle grayscaleStyle();
    /// bmh (Bayesian Methods for Hackers) style.
    FigureStyle bmhStyle();
    /// fivethirtyeight style.
    FigureStyle fivethirtyeightStyle();
    /// Solarize_Light2 style.
    FigureStyle solarizeLight2Style();
    /// fast style (minimal rendering optimizations).
    FigureStyle fastStyle();
    /// tableau-colorblind10 palette.
    FigureStyle tableauColorblind10();
    /// petroff6 color cycle.
    FigureStyle petroff6Style();
    /// petroff8 color cycle.
    FigureStyle petroff8Style();

    /// Look up a style by name (returns nullptr if not found).
    FigureStyle (*byName(const std::string& name))();
} // namespace styles

} // namespace volcano::plot
