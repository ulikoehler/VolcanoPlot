// volcano/plot/Style.hpp — matplotlib-style plot styling
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Cycler.hpp"
#include "volcano/plot/Normalize.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace volcano::plot {

/// Font description (matplotlib font_properties equivalent).
struct FontProperties {
    std::string family = "DejaVu Sans";
    std::string style = "normal";   // normal, italic, oblique
    std::string weight = "normal";  // normal, bold, light
    float size = 12.0f;             // points
    /// Text rotation in radians (screen space, Y-down; added to any
    /// built-in rotation such as the y-label's -90°).
    float rotation = 0.0f;
    /// Horizontal/vertical alignment (matplotlib ha/va).
    HAlign halign = HAlign::Center;
    VAlign valign = VAlign::Baseline;
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
    /// Whether to show minor ticks (matplotlib default: off for linear,
    /// on for log-family scales — handled automatically in rendering).
    bool minor = false;
    /// Label format, e.g. "%.2f" or "%.1e" (used when no formatter set).
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
    /// Locator/formatter overrides (matplotlib set_major_locator,
    /// set_minor_locator, set_major_formatter, set_minor_formatter).
    /// Null → automatic (scale-aware nice ticks / ScalarFormatter /
    /// AutoMinorLocator / no minor labels).
    std::shared_ptr<class Locator> locator;
    std::shared_ptr<class Locator> minorLocator;
    std::shared_ptr<class Formatter> formatter;
    std::shared_ptr<class Formatter> minorFormatter;
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
    /// Which ticks produce grid lines: "major", "minor", or "both"
    /// (matplotlib grid(which=...)).
    std::string gridWhich = "major";
    Color gridColor = Color::fromRgba8(176, 176, 176);
    float gridLineWidth = 0.8f;
    /// Grid line style: "-", "--", ":", "-."
    std::string gridLineStyle = "-";
    /// Minor-grid styling (used when gridWhich is "minor"/"both").
    Color minorGridColor = Color::fromRgba8(215, 215, 215);
    float minorGridLineWidth = 0.5f;
    std::string minorGridLineStyle = "-";
    /// Log scale.
    bool logScale = false;
    /// Label color (defaults to axis color if not set).
    Color labelColor = Color::black();
};

class IPlot;

/// One legend entry (label + handle appearance). Legend handlers
/// (LegendStyle::handlerMap) produce these for their plot type.
struct LegendHandle {
    std::string label;
    Color color = Color::black();
    LegendMarker marker = LegendMarker::Square;
};

/// Legend configuration.
struct LegendStyle {
    bool visible = false;
    /// Location: "best", "upper right", "upper left", "lower left",
    /// "lower right", "right"/"center right", "center left",
    /// "lower center", "upper center", "center", or code "0".."10".
    std::string location = "best";
    /// bbox_to_anchor: anchor point for the loc-specified corner/edge of
    /// the legend box. Unused when either coordinate is negative.
    /// anchorSpace selects the coordinate space (matplotlib bbox_transform).
    float anchorX = -1.0f, anchorY = -1.0f;
    CoordSystem anchorSpace = CoordSystem::Axes;
    /// Column/row layout (matplotlib ncols/nrows). nrows=0 → auto.
    int ncols = 1;
    int nrows = 0;
    /// Legend title row (matplotlib legend title / title_fontproperties).
    std::string title;
    FontProperties titleFont;
    FontProperties font;
    /// Label color override; nullopt → inherit style text color.
    std::optional<Color> labelColor;
    Color faceColor = Color::fromRgba8(255, 255, 255, 200);
    Color edgeColor = Color::black();
    float frameAlpha = 0.8f;
    /// Whether to draw a frame around the legend.
    bool frameOn = true;
    /// Rounded box corners (matplotlib fancybox). Adds slight padding.
    bool fancyBox = true;
    /// Draw a drop shadow behind the legend box.
    bool shadow = false;
    /// Spacing parameters in multiples of the legend font size
    /// (matplotlib legend.handlelength / handletextpad / borderpad /
    /// columnspacing / borderaxespad).
    float handleLength = 2.0f;
    float handleTextPad = 0.8f;
    float borderPad = 0.4f;
    float columnSpacing = 2.0f;
    float borderAxesPad = 0.5f;
    /// Whether the legend may be dragged (mpl legend.draggable()).
    /// While dragged, `dragOffset` accumulates the pixel displacement
    /// applied to the loc/anchor position.
    bool draggable = false;
    /// Pixel-space drag displacement applied to the resolved legend
    /// anchor. Updated by the interaction layer while dragging; users
    /// may set it to reposition the legend programmatically.
    Point2D dragOffset{0.0f, 0.0f};
    /// mpl `handler_map`: per-plot-type handlers producing legend
    /// handles. Keyed on `typeid(plot)`; a handler receives the IPlot
    /// and returns one or more handles (a plot may expand to several
    /// entries, e.g. errorbar → line + caps). Plots with no handler and
    /// no label are skipped.
    std::unordered_map<std::type_index,
        std::function<std::vector<LegendHandle>(const IPlot&)>>
        handlerMap;
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
    /// matplotlib `extend`: "neither" (default), "min", "max", or "both" —
    /// triangular extensions at the strip ends for out-of-range values.
    std::string extend = "neither";
    /// Optional custom normalization (matplotlib colorbar `norm`). When
    /// set, strip colors and tick positions map through it.
    std::shared_ptr<Normalize> norm;
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
    /// Mirrors the 'color' key of a parsed cycler expression.
    ColorCycleStyle colorCycle;

    /// Full multi-key prop_cycle entries parsed from axes.prop_cycle
    /// (e.g. cycler('color',[...]) * cycler('linestyle',[...])). When
    /// non-empty, Axes seeds its Cycler from these instead of colorCycle.
    std::vector<CycleProps> propCycle;

    /// Line style defaults.
    LineStyleDefaults lines;

    /// Patch style defaults.
    PatchStyleDefaults patch;

    /// Font family override (font.family).
    std::string fontFamily = "sans-serif";

    /// Default font size (font.size).
    float fontSize = 10.0f;

    /// Default ScalarFormatter options (axes.formatter.* rcParams),
    /// applied when no explicit formatter is set on an axis.
    std::pair<int, int> formatterLimits{-5, 6};
    bool formatterUseOffset = true;
    bool formatterUseMathText = false;

    /// Path sketch wobble amplitude in pixels (mpl `path.sketch` /
    /// plt.xkcd). 0 = off; xkcd style sets ~1.0.
    float sketchScale = 0.0f;
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
    /// petroff10 color cycle.
    FigureStyle petroff10Style();
    /// xkcd style (plt.xkcd rcParams: Comic Sans-ish font, thicker axes).
    /// Note: the hand-drawn path sketching itself is a separate feature;
    /// this captures the rcParams portion only.
    FigureStyle xkcdStyle();

    /// Look up a style by name (returns nullptr if not found).
    FigureStyle (*byName(const std::string& name))();
} // namespace styles

} // namespace volcano::plot
