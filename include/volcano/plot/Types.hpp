// volcano/plot/Types.hpp — common plot types
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

struct Point2D { float x; float y; };
struct Point3D { float x; float y; float z; };

/// Legend marker shape for a plot layer.
enum class LegendMarker {
    Square,   ///< Filled square (bar, fill, histogram, ...)
    Line,     ///< Horizontal line segment (line, step, stem, function, ...)
    Circle,   ///< Filled circle (scatter, scatter3d, ...)
};

/// Horizontal alignment (shared by text, annotations, and font properties).
enum class HAlign { Left, Center, Right };

/// Vertical alignment (shared by text, annotations, and font properties).
enum class VAlign { Bottom, Center, Top, Baseline };

/// Coordinate system for text/annotation/legend-anchor positioning.
enum class CoordSystem {
    /// Data coordinates — transformed by the axes viewport.
    Data,
    /// Axes fraction — (0,0) = bottom-left, (1,1) = top-right of axes rect.
    Axes,
    /// Figure fraction — (0,0) = bottom-left, (1,1) = top-right of figure.
    Figure,
    /// Display/pixel coordinates — (0,0) = top-left of framebuffer.
    Display,
    /// Offset in points from a data coordinate position.
    /// The position is in data coords; xyOffset is in points (1pt = 1/72 inch
    /// at the figure DPI). Positive x = right, positive y = up.
    OffsetPoints,
};

/// 2D extent (width, height) in pixels.
struct Extent2D { uint32_t width = 0; uint32_t height = 0; };

/// 2D rectangle (offset + extent) in pixels.
struct Rect2D { int32_t x = 0; int32_t y = 0; uint32_t width = 0; uint32_t height = 0; };

/// Float rectangle in arbitrary units (treemap/layout helpers).
struct Rect2Df { float x = 0, y = 0, w = 0, h = 0; };

/// RGBA color, normalized [0,1].
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    static constexpr Color black()   { return {0,0,0,1}; }
    static constexpr Color white()   { return {1,1,1,1}; }
    static constexpr Color red()     { return {1,0,0,1}; }
    static constexpr Color green()   { return {0,1,0,1}; }
    static constexpr Color blue()    { return {0,0,1,1}; }
    static constexpr Color transparent() { return {0,0,0,0}; }
    /// Construct from 8-bit RGBA.
    static constexpr Color fromRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r/255.0f, g/255.0f, b/255.0f, a/255.0f};
    }

    /// Parse a matplotlib-style color string. Supports:
    ///   - Single-letter shorthands: "b", "g", "r", "c", "m", "y", "k", "w"
    ///   - Hex: "#RGB", "#RRGGBB", "#RGBA", "#RRGGBBAA"
    ///   - Named CSS4 colors: "red", "blue", "lightblue", etc.
    ///   - CN cycle colors: "C0"–"C9" (uses the default color cycle)
    ///   - Grayscale: "0.0"–"1.0" (string of a float in [0,1])
    /// Returns nullopt on parse failure.
    static std::optional<Color> parse(std::string_view s);

    /// Same as parse(), but returns black on failure (for convenience).
    static Color parseOr(std::string_view s, Color fallback = black());
};

/// mpl patheffects: an under-draw pass applied before the artist's own
/// draw (matplotlib.patheffects). A list of effects attaches to an
/// artist via `path_effects`; each pass re-renders the same path with
/// modified graphics state in list order. `withStroke`-style classes
/// set `thenNormal` — the normal draw runs right after that pass.
struct PathEffect {
    enum class Kind {
        Normal,       // draw the artist normally
        Stroke,       // mpl Stroke — under-stroke with gc overrides
        LineShadow,   // mpl SimpleLineShadow — offset stroked copy
        PatchShadow,  // mpl SimplePatchShadow — offset filled copy
    };
    Kind kind = Kind::Normal;
    /// mpl `offset` in points (x right, y up) applied to this pass.
    Point2D offset{0.0f, 0.0f};
    /// mpl `withX` subclasses: draw the artist normally after this pass.
    bool thenNormal = false;
    /// Stroke gc overrides — unset keeps the artist's own width/color
    /// (mpl _update_gc semantics).
    std::optional<float> lineWidth;   // points
    std::optional<Color> foreground;
    /// mpl gc alpha override for the pass (Stroke/etc.); unset keeps
    /// the artist's alpha.
    std::optional<float> alpha;
    /// Shadow color — nullopt = mpl default (base rgb × rho for
    /// PatchShadow; 'k' is the SimpleLineShadow default so Python passes
    /// it explicitly).
    std::optional<Color> shadowColor;
    float shadowAlpha = 0.3f;  // mpl alpha default
    float rho = 0.3f;          // mpl rho default

    /// mpl shadow color resolution: explicit color, else base × rho.
    /// Alpha is replaced by shadowAlpha (mpl gc.set_alpha).
    [[nodiscard]] Color shadowFor(Color base) const {
        Color c = shadowColor
            ? *shadowColor
            : Color{base.r * rho, base.g * rho, base.b * rho, 1.0f};
        c.a = shadowAlpha;
        return c;
    }
    /// Offset in display pixels (1pt = dpi/72 px; mpl offset y is up,
    /// display is Y-down).
    [[nodiscard]] Point2D offsetPx(float dpi) const {
        return {offset.x * dpi / 72.0f, -offset.y * dpi / 72.0f};
    }
    /// Stroke pass width in px (falls back to `basePx`).
    [[nodiscard]] float strokeWidthPx(float basePx, float dpi) const {
        return lineWidth ? *lineWidth * dpi / 72.0f : basePx;
    }
};

/// One legend entry (label + handle appearance). Plots with multiple
/// legend items (e.g. per-series) produce several handles.
struct LegendHandle {
    std::string label;
    Color color = Color::black();
    LegendMarker marker = LegendMarker::Square;
    /// Markers drawn on the handle (mpl numpoints/scatterpoints):
    ///   -1 = auto — Circle → legend.scatterpoints, others → none
    ///   -2 = legend.numpoints (plots set this when the artist carries
    ///        markers, e.g. a marked Line2D)
    ///   >=0 = explicit count.
    int points = -1;
};

/// Default color cycle (matplotlib's "tab10" palette).
/// Used by "C0"–"C9" color strings and for automatic plot coloring.
class ColorCycle {
public:
    /// Get the color at index i (wraps around modulo 10).
    [[nodiscard]] static Color at(size_t i);
    /// Number of colors in the default cycle.
    static constexpr size_t size() { return 10; }
};

/// mpl `sticky_edges`: data-space values the autoscale margin must not
/// cross (e.g. a bar's baseline at 0 keeps ylim from padding below it).
struct StickyEdges { std::vector<float> x, y; };

/// Axis range in data coordinates.
struct Range {
    float min = 0.0f;
    float max = 1.0f;
    [[nodiscard]] float span() const noexcept { return max - min; }
    [[nodiscard]] bool valid() const noexcept { return max > min; }
};

/// 2D viewport in data coordinates.
struct Viewport {
    Range x{0,1};
    Range y{0,1};
    Range z{0,1}; // for 3D
};

/// Marker style for scatter plots. Codes mirror matplotlib's marker set;
/// numeric codes are passed to the SDF marker shader as floats.
enum class MarkerStyle {
    Point = 0,        ///< '.'
    Circle = 1,       ///< 'o'
    Square = 2,       ///< 's'
    Diamond = 3,      ///< 'D'
    ThinDiamond = 4,  ///< 'd'
    Triangle = 5,     ///< '^'
    TriDown = 6,      ///< 'v'
    TriLeft = 7,      ///< '<'
    TriRight = 8,     ///< '>'
    Tri1 = 9,         ///< '1' tripod (Y)
    Tri2 = 10,        ///< '2'
    Tri3 = 11,        ///< '3'
    Tri4 = 12,        ///< '4'
    Plus = 13,        ///< '+' (stroked)
    X = 14,           ///< 'x' (stroked)
    PlusFilled = 15,  ///< 'P' (filled)
    XFilled = 16,     ///< 'X' (filled)
    Star = 17,        ///< '*'
    Pentagon = 18,    ///< 'p'
    Hexagon1 = 19,    ///< 'h'
    Hexagon2 = 20,    ///< 'H'
    Octagon = 21,     ///< '8'
    VLine = 22,       ///< '|'
    HLine = 23,       ///< '_'
    TickLeft = 24,    ///< TICKLEFT  ('0')
    TickRight = 25,   ///< TICKRIGHT ('1' — note: distinct from Tri1)
    TickUp = 26,      ///< TICKUP    ('2')
    TickDown = 27,    ///< TICKDOWN  ('3')
    CaretLeft = 28,       ///< CARETLEFT    ('4')
    CaretRight = 29,      ///< CARETRIGHT   ('5')
    CaretUp = 30,         ///< CARETUP      ('6')
    CaretDown = 31,       ///< CARETDOWN    ('7')
    CaretLeftBase = 32,   ///< CARETLEFTBASE  ('8')
    CaretRightBase = 33,  ///< CARETRIGHTBASE ('9')
    CaretUpBase = 34,     ///< CARETUPBASE    ('10')
    CaretDownBase = 35,   ///< CARETDOWNBASE  ('11')
    Polygon = 36,     ///< (numsides, 0, angle) regular polygon
    StarN = 37,       ///< (numsides, 1, angle) star-like polygon
    AsteriskN = 38,   ///< (numsides, 2, angle) asterisk
    CircledN = 39,    ///< (numsides, 3, angle) circle approx by n-gon
    None = 40,
    Pixel = 41,       ///< ',' single-pixel square
};

/// Marker fill style (matplotlib fillstyle).
enum class MarkerFill {
    Full = 0, Left = 1, Right = 2, Bottom = 3, Top = 4, None = 5,
};

/// Parse a single-character matplotlib marker spec ('o', '^', '<', ...).
/// Returns nullopt for unknown characters.
std::optional<MarkerStyle> markerFromChar(char c);

/// Parse a matplotlib fillstyle name: "full", "left", "right", "bottom",
/// "top", "none".
std::optional<MarkerFill> markerFillFromString(std::string_view s);

/// Line style.
enum class LineStyle {
    Solid, Dashed, Dotted, DashDot, None
};

/// Parse a matplotlib linestyle spec: "-", "--", "-.", ":", "solid",
/// "dashed", "dashdot", "dotted", "none"/"None"/"" (empty → None).
std::optional<LineStyle> lineStyleFromString(std::string_view s);

/// Standard dash pattern for a named line style, scaled by line width
/// (matplotlib: dash lengths are in points × linewidth). Returns empty for
/// Solid/None.
std::vector<float> dashPattern(LineStyle style, float width);

/// How a polyline's vertices are joined (matplotlib joinstyle).
enum class JoinStyle { Miter, Round, Bevel };

/// How open polyline ends are capped (matplotlib capstyle).
enum class CapStyle { Butt, Round, Projecting };

/// Parse "miter"/"round"/"bevel" / "butt"/"round"/"projecting".
std::optional<JoinStyle> joinStyleFromString(std::string_view s);
std::optional<CapStyle> capStyleFromString(std::string_view s);

/// Draw style: how points are connected (matplotlib drawstyle).
enum class DrawStyle {
    Default,    ///< straight line segments
    StepsPre,   ///< step, y value continued to the left  ("steps-pre")
    StepsMid,   ///< step, change at midpoint            ("steps-mid")
    StepsPost,  ///< step, y value continued to the right ("steps-post")
};

/// Parse "default"/"steps"/"steps-pre"/"steps-mid"/"steps-post".
/// ("steps" is an alias for "steps-pre".)
std::optional<DrawStyle> drawStyleFromString(std::string_view s);

/// Expand a point sequence for a step draw style. Returns `points`
/// unchanged for DrawStyle::Default.
std::vector<Point2D> applyDrawStyle(std::span<const Point2D> points,
                                    DrawStyle style);

} // namespace volcano::plot
