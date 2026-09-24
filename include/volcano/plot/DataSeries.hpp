// volcano/plot/DataSeries.hpp — data series abstraction
#pragma once

#include "volcano/plot/Types.hpp"

#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <volcano/plot/Path.hpp>

#include <optional>
#include <vector>

namespace volcano::plot {

/// mpl `markevery` spec: subsamples the marker positions of a line.
/// - `int N` → every N-th point starting at index 0
/// - `std::pair<int,int>` (start, step) → every step-th point from
///   index `start` (negative start counts from the end, slice-style)
/// - `float f` → markers spaced ~`f` × the axes-box display diagonal
///   apart along the polyline's display-space arc length
/// - `std::pair<float,float>` (start, step) → same, first marker offset
///   by `start` × the diagonal
/// - `std::vector<int>` → mpl fancy indexing: markers at those indices
/// An empty `std::optional` draws a marker at every point (every=None).
using MarkerEvery = std::variant<int, std::pair<int, int>, float,
                                 std::pair<float, float>,
                                 std::vector<int>>;

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

    /// mpl `alpha`: uniform opacity multiplier applied to the series
    /// color and marker colors at draw time (1 = opaque).
    float alpha = 1.0f;
    /// The color to draw with: `color` unless still `autoColor()`;
    /// `alpha` multiplies the result (mpl alpha propagates to every
    /// place the artist color is used, including the legend handle).
    [[nodiscard]] Color resolvedColor() const {
        Color c = color.a > 0 ? color : ColorCycle::at(0);
        c.a *= alpha;
        return c;
    }
    /// Alpha-adjusted optional color (marker face/edge overrides).
    [[nodiscard]] std::optional<Color> applyAlpha(
            std::optional<Color> c) const {
        if (c) c->a *= alpha;
        return c;
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
    /// mpl `marker=` string spec: '$…$' selects a mathtext glyph
    /// marker, "" / "None" / "none" disables markers, a single
    /// character selects the corresponding MarkerStyle. Returns false
    /// for unrecognized specs (matplotlib raises ValueError).
    bool setMarker(std::string_view spec) {
        markerTex.clear();
        markerPath.reset();
        if (spec.empty() || spec == "None" || spec == "none") {
            marker = MarkerStyle::None;
            return true;
        }
        if (spec.size() >= 2 && spec.front() == '$' && spec.back() == '$') {
            markerTex = std::string(spec.substr(1, spec.size() - 2));
            return true;
        }
        if (spec.size() == 1)
            if (auto m = markerFromChar(spec[0])) { marker = *m; return true; }
        return false;
    }
    /// mpl `markerfacecolor`: nullopt → 'auto' (the series color);
    /// transparent → 'none' (unfilled); opaque → explicit fill color.
    std::optional<Color> markerFaceColor;
    /// mpl `markeredgecolor` (same auto/none/color scheme).
    std::optional<Color> markerEdgeColor;
    /// mpl `markeredgewidth` (pixels).
    float markerEdgeWidth = 1.0f;
    /// mpl `markevery` (None = a marker at every point). Only the
    /// marker positions are subsampled — the line itself is untouched.
    std::optional<MarkerEvery> markevery;
    /// mpl set_markevery: every N-th point, or (start, step) count form.
    void setMarkevery(int every) { markevery = every; }
    void setMarkevery(int start, int step) {
        markevery = std::pair{start, step};
    }
    /// mpl float form: spacing as a fraction of the axes-box diagonal.
    void setMarkevery(float every) { markevery = every; }
    void setMarkevery(float start, float step) {
        markevery = std::pair{start, step};
    }
    void setMarkevery(double every) { markevery = float(every); }
    void setMarkevery(double start, double step) {
        markevery = std::pair{float(start), float(step)};
    }
    /// mpl fancy indexing: markers only at the given point indices.
    void setMarkevery(std::vector<int> indices) {
        markevery = std::move(indices);
    }
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
    /// mpl RGB(A) imshow data: width*height RGBA8 packed as uint32
    /// (little-endian R|G<<8|B<<16|A<<24). When non-empty the grid is
    /// rendered directly, bypassing the colormap LUT and valueRange.
    std::vector<uint32_t> rgba;
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
    /// mpl `explode`: radial offset per wedge in pie data units
    /// (fraction of `radius` when radius == 1). One element applies to
    /// all wedges — an extension of mpl, which requires len == len(x).
    std::vector<float> explode;
    /// mpl `startangle`: degrees counterclockwise from the +x axis
    /// where the first wedge starts (default 0 → 3 o'clock).
    float startAngle = 0.0f;
    /// mpl `counterclock`: wedge ordering direction (default true).
    bool counterclock = true;
    /// mpl `radius`: pie radius in data units — the axes view spans
    /// ±1.25, so radius 1 (default) fills 80% of the half-extent.
    float radius = 1.0f;
    /// mpl `center`: pie center in the same data units ((0,0) = axes
    /// center).
    Point2D center{0.0f, 0.0f};
    /// mpl `normalize`: divide values by their sum (default true).
    /// When false a sum < 1 draws a partial pie.
    bool normalize = true;
    /// mpl `autopct`: printf-style format fed 100·frac (e.g. "%1.1f%%").
    /// Empty → no percentage labels (mpl autopct=None).
    std::string autopct;
    /// mpl callable `autopct`: returns the label for 100·frac.
    /// Takes precedence over the `autopct` format string.
    std::function<std::string(double)> autopctFn;
    /// mpl `pctdistance`: autopct label distance × radius (default 0.6).
    float pctDistance = 0.6f;
    /// mpl `labeldistance`: category-label distance × radius
    /// (default 1.1). NaN disables labels (mpl labeldistance=None).
    float labelDistance = 1.1f;
    /// mpl `rotatelabels`: rotate each category label to its wedge
    /// midpoint angle.
    bool rotatelabels = false;
    /// mpl `frame`: when false (mpl default), Axes::pie() hides the
    /// axes frame and ticks and sets the view to the pie extent.
    bool frame = false;
};

/// Variant of all data kinds.
using DataVariant = std::variant<Series2D, Series3D, Grid2D, FunctionEval, BarData, PieData>;

} // namespace volcano::plot
