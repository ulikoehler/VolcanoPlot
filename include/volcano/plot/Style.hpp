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
    int nbins = 10;
    /// If set, fixed tick positions.
    std::optional<std::vector<float>> positions;
    /// If set, fixed tick labels (parallel to positions).
    std::optional<std::vector<std::string>> labels;
    /// Whether to show minor ticks.
    bool minor = true;
    /// Label format, e.g. "%.2f" or "%.1e".
    std::string format = "%g";
};

/// Axis appearance configuration.
struct AxisStyle {
    bool visible = true;
    Color color = Color::black();
    float lineWidth = 1.0f;
    std::string label;       // axis label text
    FontProperties labelFont;
    FontProperties tickFont;
    TickConfig ticks;
    /// Grid lines on this axis.
    bool grid = true;
    Color gridColor = Color::fromRgba8(200, 200, 200);
    float gridLineWidth = 0.5f;
    /// Log scale.
    bool logScale = false;
};

/// Legend configuration.
struct LegendStyle {
    bool visible = false;
    std::string location = "best"; // best, upper right, upper left, lower right, ...
    FontProperties font;
    Color faceColor = Color::fromRgba8(255, 255, 255, 200);
    Color edgeColor = Color::black();
    float frameAlpha = 0.8f;
};

/// Title configuration.
struct TitleStyle {
    std::string text;
    FontProperties font;
    Color color = Color::black();
    float pad = 6.0f;
};

/// Overall figure style (matplotlib rcParams subset).
struct FigureStyle {
    Color faceColor = Color::white();
    Color edgeColor = Color::black();
    float dpi = 100.0f;
    std::string styleName = "default"; // ggplot, seaborn, default, ...
    TitleStyle title;
    LegendStyle legend;
    AxisStyle xAxis;
    AxisStyle yAxis;
    AxisStyle zAxis; // for 3D
};

/// Built-in style presets (matplotlib style sheets equivalent).
namespace styles {
    FigureStyle defaultStyle();
    FigureStyle ggplotStyle();
    FigureStyle seabornStyle();
    FigureStyle darkBackground();
    FigureStyle grayscale();
} // namespace styles

} // namespace volcano::plot
