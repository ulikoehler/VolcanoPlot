// volcano/plot/DataSeries.hpp — data series abstraction
#pragma once

#include "volcano/plot/Types.hpp"

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <volcano/plot/Path.hpp>

#include <optional>
#include <vector>

namespace volcano::plot {

/// A series of 2D points (scatter, line, etc.).
struct Series2D {
    /// Sentinel for "take the color from the axes prop_cycle"
    /// (matplotlib plot()/scatter() default). Fully transparent.
    static constexpr Color autoColor() { return Color::transparent(); }

    std::vector<Point2D> points;
    std::string label;
    /// The draw color. `autoColor()` (transparent) means the axes prop
    /// cycle assigns it at addPlot() time; `resolvedColor()` falls back
    /// to cycle color 0 when no cycle ever resolved it.
    Color color = autoColor();

    /// The color to draw with: `color` unless still `autoColor()`.
    [[nodiscard]] Color resolvedColor() const {
        return color.a > 0 ? color : ColorCycle::at(0);
    }
    /// True while the color is the auto/cycle sentinel.
    [[nodiscard]] bool hasAutoColor() const { return color.a == 0; }
    float size = 6.0f;          // marker size in pixels
    MarkerStyle marker = MarkerStyle::None;  // mpl plot() default: none
    MarkerFill markerFill = MarkerFill::Full;
    int markerNumsides = 5;     ///< for Polygon/StarN/AsteriskN/CircledN
    float markerAngle = 0.0f;   ///< rotation in radians
    /// Custom marker path (matplotlib marker=Path instance): vertex
    /// bounds are normalized to the marker-size box. Overrides `marker`.
    std::optional<Path> markerPath;
    /// TeX/mathtext marker (matplotlib marker='$…$'): the laid-out
    /// glyph string is drawn centered at each point. Overrides `marker`.
    std::string markerTex;
    LineStyle lineStyle = LineStyle::Solid;
    /// Custom dash tuple (on, off, ...) in pixels. Empty → derived from
    /// lineStyle via dashPattern().
    std::vector<float> dashes;
    float dashOffset = 0.0f;
    float lineWidth = 1.5f;
    DrawStyle drawStyle = DrawStyle::Default;
    JoinStyle joinStyle = JoinStyle::Round;
    CapStyle capStyle = CapStyle::Butt;
    /// Color of the gaps between dashes (alpha 0 → no gap color).
    Color gapColor = Color::transparent();
    /// When true, Axes::addPlot applies the axes' prop_cycle entry to
    /// this series (color/lineStyle/lineWidth/marker when the cycle
    /// provides them) and advances the cycle. Set before addPlot;
    /// fields may still be overridden after addPlot via series().
    /// matplotlib parity: plot()/scatter() consume the prop_cycle by
    /// default, so this is on unless the caller opts out.
    bool usePropCycle = true;
};

/// A series of 3D points (3D scatter, surface).
struct Series3D {
    std::vector<Point3D> points;
    std::string label;
    Color color = Color::blue();
    float size = 6.0f;
};

/// A 2D grid of scalar values (heatmap, surface, KDE).
struct Grid2D {
    std::vector<float> values; // width*height, row-major
    uint32_t width = 0;
    uint32_t height = 0;
    Range xRange{0,1};
    Range yRange{0,1};
    /// Optional value range; if invalid (min > max), computed from data.
    Range valueRange{std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::lowest()};
    /// Grid row origin: "upper" places row 0 at the top of the axes
    /// (matplotlib imshow default), "lower" places it at the bottom.
    std::string origin = "upper";
    /// mpl imshow `interpolation`: "nearest" (default, discrete cells),
    /// "bilinear", "bicubic", or "antialiased" (approximated by bilinear
    /// on the GPU; mpl maps it to its auto/hanning resampler).
    std::string interpolation = "nearest";
};

/// A function to be evaluated on the GPU.
struct FunctionEval {
    /// GLSL expression body, e.g. "sin(x)*cos(y)".
    /// The variables x (and y for 2D) are available.
    std::string glslBody;
    /// For 1D functions: number of samples to evaluate.
    uint32_t samples = 1024;
    Range xRange{-10, 10};
    Range yRange{-10, 10}; // for 2D functions
    bool is2D = false;
};

/// Bar chart data.
struct BarData {
    std::vector<float> heights;
    std::vector<std::string> labels;
    std::vector<Color> colors; // optional per-bar colors
    float width = 0.8f;
    bool horizontal = false;
};

/// Pie chart data.
struct PieData {
    std::vector<float> values;
    std::vector<std::string> labels;
    std::vector<Color> colors;
    bool donut = false;       // 3D pie / donut
    float innerRadius = 0.0f; // for donut
    float explode = 0.0f;
};

/// Variant of all data kinds.
using DataVariant = std::variant<Series2D, Series3D, Grid2D, FunctionEval, BarData, PieData>;

} // namespace volcano::plot
