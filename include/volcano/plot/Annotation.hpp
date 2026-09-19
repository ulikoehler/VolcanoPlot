// volcano/plot/Annotation.hpp — text annotations and arrows
//
// Provides matplotlib-style ax.text() and ax.annotate() functionality.
// Text can be placed in data, axes-fraction, figure-fraction, or display
// (pixel) coordinates. Annotations connect a text label to a data point
// with an optional arrow.
//
// Usage:
//   ax.text(3.0, 4.0, "peak", CoordSystem::Data);
//   ax.annotate(3.0, 4.0, 5.0, 7.0, "maximum", CoordSystem::Data);
#pragma once

#include "volcano/plot/Path.hpp"
#include "volcano/plot/Types.hpp"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace volcano::plot {

// CoordSystem and HAlign/VAlign live in Types.hpp (shared with
// FontProperties/LegendStyle).

/// Arrow style for annotations.
enum class ArrowStyle {
    None,       ///< No arrow (just a line).
    Simple,     ///< Simple line with arrowhead.
    Fancy,      ///< FancyArrowPatch-style with curved shaft.
    Wedge,      ///< Wedge-shaped arrowhead.
    Arrow,      ///< Standard arrow (matplotlib '->').
    ArrowSmall, ///< Small arrow (matplotlib '->' with small head).
};

/// Connection path style between the text and the annotated point
/// (matplotlib connectionstyle). Parameters mirror matplotlib:
///   "arc3,rad=0.3"    — quadratic bezier, control offset rad·|AB|/2
///   "arc,angleA=a,angleB=b,rad=r" — rounded joint at angles
///   "angle,angleA=a,angleB=b,rad=r" — two-segment kinked path
///   "bar,fraction=0.3" — straight path with a perpendicular notch
struct ConnectionStyle {
    enum class Kind { Arc3, Arc, Angle, Bar } kind = Kind::Arc3;
    float rad = 0.0f;          ///< arc3/arc/angle: curvature radius param
    float angleA = 0.0f;       ///< arc/angle: entry angle at A (degrees)
    float angleB = 90.0f;      ///< arc/angle: exit angle at B (degrees)
    float fraction = 0.3f;     ///< bar: notch height as fraction of |AB|
};

/// Parse a matplotlib connectionstyle string such as
/// "arc3,rad=-0.2", "angle,angleA=0,angleB=90,rad=10", "bar,fraction=0.3".
/// Unrecognized strings fall back to Arc3 with rad=0 (straight line).
ConnectionStyle parseConnectionStyle(std::string_view s);

/// matplotlib `arrowstyle` specification (FancyArrowPatch). Two kinds:
///   - end-marked curves: "-", "<-", "->", "<->", "-[", "<-[", "]-[",
///     "-|>", "<|-", "<|-|>", "|-|" — stroke/fill marks at the path ends
///   - named full-body arrows: "simple", "fancy", "wedge" — a single
///     filled polygon swept along the connection path
/// Head/body dimensions are in units of `mutationSize`, matching
/// matplotlib's mutation_scale scaling: mutationSize is in *points*
/// (mpl default = annotation fontsize = 10pt) and is converted to
/// pixels by × dpi/72 before geometry generation.
struct ArrowStyleSpec {
    /// Mark at a path end. A=path start (text end), B=path end (data end).
    enum class End { None, Open, Filled, Bracket, Bar };
    /// Named full-body arrow style (overrides ends when set).
    enum class Body { None, Simple, Fancy, Wedge };

    End headA = End::None;
    End headB = End::None;
    Body body = Body::None;

    float headLength = 0.4f;   ///< × mutationSize
    float headWidth = 0.2f;    ///< × mutationSize
    float tailWidth = 0.2f;    ///< × mutationSize (named bodies)
    float widthA = 1.0f;       ///< bracket/bar width at A × mutationSize
    float widthB = 1.0f;       ///< bracket/bar width at B × mutationSize
    float lengthA = 0.2f;      ///< shaft shrink at A × mutationSize
    float lengthB = 0.2f;      ///< shaft shrink at B × mutationSize
    float shrinkFactor = 0.5f; ///< wedge: position of max width
    float mutationSize = 10.0f;///< points (mpl mutation_scale = fontsize)
};

/// Parse a matplotlib arrowstyle string, e.g. "->", "-|>",
/// "<|-|>, head_length=0.6", "wedge, tail_width=0.5".
/// Unknown names fall back to "->" (mpl default is "simple", but "->"
/// matches the existing VolcanoPlot annotation default).
ArrowStyleSpec parseArrowStyle(std::string_view s);

/// Geometry produced by transmuting an arrowstyle onto a sampled
/// connection path (pixel space). Strokes are drawn as line strips,
/// fills as triangle-soup polygons (via earClip).
struct ArrowGeometry {
    std::vector<std::vector<Point2D>> strokes;
    std::vector<std::vector<Point2D>> fills;
};

/// Transmute `spec` onto the sampled connection `path` (pixel space,
/// at least 2 points). `lineWidth` feeds bracket/bar thickness.
ArrowGeometry buildArrowGeometry(std::span<const Point2D> path,
                                 const ArrowStyleSpec& spec,
                                 float lineWidth = 1.0f);

/// Sample the connection path between A (text end) and B (data end),
/// in pixel space. Returns a polyline including both endpoints.
/// shrinkA/shrinkB pull the endpoints inward along the local tangent.
std::vector<Point2D> connectionPath(Point2D a, Point2D b,
                                    const ConnectionStyle& style,
                                    float shrinkA = 0.0f, float shrinkB = 0.0f,
                                    int samples = 24);

/// A text annotation placed at a position in a given coordinate system.
struct TextAnnotation {
    /// Position in the specified coordinate system.
    float x = 0.0f, y = 0.0f;
    /// Coordinate system for (x, y).
    CoordSystem coords = CoordSystem::Data;
    /// For OffsetPoints: offset in points from the data position.
    float xyOffsetX = 0.0f, xyOffsetY = 0.0f;

    /// Text content (UTF-8).
    std::string text;

    /// Font size scale (1.0 = default 16px).
    float fontSize = 1.0f;
    /// Text color.
    Color color = Color::black();
    /// Rotation in radians (clockwise, screen space Y-down).
    float rotation = 0.0f;
    /// Horizontal alignment.
    HAlign halign = HAlign::Left;
    /// Vertical alignment.
    VAlign valign = VAlign::Baseline;

    /// Optional background box color (alpha=0 = no background).
    Color bboxFaceColor = Color::transparent();
    /// Optional background box edge color (alpha=0 = no edge).
    Color bboxEdgeColor = Color::transparent();
    /// Background box padding in pixels.
    float bboxPadding = 4.0f;
    /// Background box corner radius in pixels (0 = square corners).
    float bboxCornerRadius = 0.0f;
    /// mpl bbox=dict(boxstyle=...) — when set, the bbox outline uses this
    /// boxstyle path instead of a plain rectangle.
    std::optional<BoxStyleSpec> boxStyle;

    /// Whether to clip the text to the axes rect (matplotlib clip_on).
    /// Default false (matplotlib default); set true to clip data-space text.
    bool clipOn = false;

    /// mpl draggable(): allow dragging the text with the mouse.
    bool draggable = false;
    /// Accumulated pixel-space drag displacement (figure px, Y-down).
    mutable Point2D dragOffset{0.0f, 0.0f};
    /// Text bounding box in figure px from the last draw (hit-testing).
    /// Set by the renderers; empty when never drawn.
    mutable Rect2D drawBox{};
};

/// An annotation with an arrow connecting text to a data point.
/// Equivalent to matplotlib's ax.annotate(text, xy, xytext, arrowprops).
struct Annotation {
    /// The point being annotated (typically in data coordinates).
    float xy[2] = {0.0f, 0.0f};
    CoordSystem xyCoords = CoordSystem::Data;

    /// The text position (where the label is placed).
    float xyText[2] = {0.0f, 0.0f};
    CoordSystem xyTextCoords = CoordSystem::Data;
    /// For OffsetPoints text coords: offset in points from xy.
    float textOffsetX = 0.0f, textOffsetY = 0.0f;

    /// Text content.
    std::string text;

    /// Font size scale.
    float fontSize = 1.0f;
    /// Text color.
    Color color = Color::black();
    /// Text alignment.
    HAlign halign = HAlign::Left;
    VAlign valign = VAlign::Baseline;

    /// Arrow style (None = no arrow, just text).
    ArrowStyle arrowStyle = ArrowStyle::Simple;
    /// Optional mpl arrowstyle spec (e.g. `parseArrowStyle("-|>")`).
    /// When set, takes precedence over `arrowStyle`/`arrowHeadSize`/
    /// `arrowHeadAngle`.
    std::optional<ArrowStyleSpec> arrowSpec;
    /// Arrow color.
    Color arrowColor = Color::black();
    /// Arrow line width in pixels.
    float arrowWidth = 1.0f;
    /// Arrowhead size in pixels (length of the arrowhead).
    float arrowHeadSize = 10.0f;
    /// Arrowhead opening angle in degrees.
    float arrowHeadAngle = 30.0f;

    /// Connection path style (matplotlib connectionstyle; default
    /// arc3 with rad=0, i.e. a straight line). Set via
    /// `a.connection = parseConnectionStyle("arc3,rad=0.3")`.
    ConnectionStyle connection;

    /// Whether to clip the text/arrow to the axes rect (matplotlib clip_on).
    bool clipOn = false;

    /// Optional background box for the text.
    Color bboxFaceColor = Color::transparent();
    Color bboxEdgeColor = Color::transparent();
    float bboxPadding = 4.0f;
    /// mpl bbox=dict(boxstyle=...) — outline path for the bbox.
    std::optional<BoxStyleSpec> boxStyle;

    /// Shrink the arrow on both ends by this many pixels
    /// (so it doesn't overlap the text or the data point marker).
    float shrinkA = 2.0f;  ///< shrink at the text end
    float shrinkB = 2.0f;  ///< shrink at the data point end

    /// mpl set_clip_path: optional clip path in data coords; the arrow
    /// geometry is clipped to its outline at draw time.
    std::optional<Path> clipPath;

    /// mpl draggable(): allow dragging the annotation text (the arrow
    /// follows, keeping its data-space anchor).
    bool draggable = false;
    /// Accumulated pixel-space drag displacement (figure px, Y-down).
    mutable Point2D dragOffset{0.0f, 0.0f};
    /// Text bounding box in figure px from the last draw (hit-testing).
    mutable Rect2D drawBox{};
};

/// An anchored scale bar (mpl_toolkits.axes_grid1 `AnchoredSizeBar`):
/// a horizontal bar of `size` data-x units with a centered label,
/// anchored at `loc` inside the axes with an optional frame.
struct SizeBar {
    /// Horizontal bar length in data-x units (mpl `size`).
    float size = 0.0f;
    /// Label text under (or over, when labelTop) the bar.
    std::string label;
    /// Anchor location — same names as legend loc ("lower right",
    /// "upper left", ... or mpl numeric codes "1".."9").
    std::string loc = "lower right";
    /// Padding inside the frame, fraction of the font size (mpl pad).
    float pad = 0.1f;
    /// Padding between the frame and the axes edge, fraction of the
    /// font size (mpl borderpad).
    float borderpad = 0.1f;
    /// Separation between bar and label in points (mpl sep).
    float sep = 2.0f;
    /// Draw a frame box around bar + label (mpl frameon).
    bool frameon = true;
    /// Bar height in data-y units (mpl size_vertical). 0 = thin line.
    float sizeVertical = 0.0f;
    /// Bar and label color.
    Color color = Color::black();
    /// Label above the bar instead of below (mpl label_top).
    bool labelTop = false;
    /// Fill the bar instead of stroking its outline (mpl fill_bar;
    /// default: fill when sizeVertical > 0).
    std::optional<bool> fillBar;
    /// Font size scale (1.0 = default 16px).
    float fontSize = 1.0f;
    /// Frame fill color (mpl legend-style white@0.8).
    Color frameFaceColor{1.0f, 1.0f, 1.0f, 0.8f};
};

class Axes;

/// Text metrics for anchored-artist layout (width/height/ascent, px).
struct SizeBarTextMeasure { float width = 0, height = 0, ascent = 0; };

/// An anchored text box (mpl_toolkits.axes_grid1 `AnchoredText`):
/// text anchored at `loc` inside the axes with an optional frame.
struct AnchoredText {
    /// Text content (multi-line via '\n').
    std::string text;
    /// Anchor location — mpl loc names or numeric codes ("1".."10").
    std::string loc = "upper left";
    /// Padding inside the frame, fraction of font size (mpl pad=0.4).
    float pad = 0.4f;
    /// Padding between frame and axes edge, fraction of font size
    /// (mpl borderpad=0.5).
    float borderpad = 0.5f;
    /// Draw a frame box (mpl frameon=True).
    bool frameon = true;
    /// Font size scale (1.0 = default 16px).
    float fontSize = 1.0f;
    Color color = Color::black();
    /// mpl patch facecolor/edgecolor defaults ('white'/'0.8' at 0.8 alpha
    /// via the offset-box frame).
    Color frameFaceColor{1.0f, 1.0f, 1.0f, 0.8f};
    Color frameEdgeColor{0.8f, 0.8f, 0.8f, 0.8f};
};

/// Pixel-space layout of an AnchoredText (shared by raster + vector).
struct AnchoredTextLayout {
    Rect2Df box;                          ///< frame rect
    std::vector<Point2D> lineBaselines;   ///< one baseline origin per line
    bool valid = false;
};

AnchoredTextLayout layoutAnchoredText(
    const AnchoredText& at, const Axes& axes, Rect2D axesRect,
    const std::function<SizeBarTextMeasure(std::string_view,
                                           float)>& measure);

/// Pixel-space layout of a SizeBar (shared by raster + vector paths).
/// A zoom indicator rectangle + connectors drawn on a parent axes,
/// marking the data region shown by an inset axes
/// (mpl `Axes.indicate_inset` / `indicate_inset_zoom`).
struct InsetIndicator {
    /// The inset axes this indicator points to (owns rect + viewport).
    /// May be null when explicit `bounds` are given without connectors.
    const Axes* inset = nullptr;
    /// Explicit rectangle in this axes' data coords (mpl `bounds`,
    /// transData default). When false, the inset axes' viewport limits
    /// supply the rectangle.
    bool hasBounds = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    /// Rectangle fill (mpl facecolor; 'none' default → transparent).
    Color faceColor = Color::transparent();
    /// Rectangle edge + connector color (mpl edgecolor '0.5').
    Color edgeColor{0.5f, 0.5f, 0.5f, 1.0f};
    /// mpl alpha (multiplies both colors).
    float alpha = 0.5f;
    /// Connector line width in px (mpl linewidth, patch default 1.0).
    float lineWidth = 1.0f;
    /// Per-corner connector visibility [LL, UL, LR, UR]; nullopt = auto
    /// (mpl picks the two corners that don't overlap the inset box).
    std::optional<std::array<bool, 4>> connectors;
};

/// Pixel-space layout of an InsetIndicator (shared by raster + vector).
struct InsetIndicatorLayout {
    Rect2Df rect;   ///< indicator rectangle, figure px
    /// px segments: rect-corner → inset-axes corner, order LL,UL,LR,UR.
    std::array<std::pair<Point2D, Point2D>, 4> connectors{};
    std::array<bool, 4> connVisible{};
    bool valid = false;
};

InsetIndicatorLayout layoutInsetIndicator(const InsetIndicator& ind,
                                          const Axes& parent,
                                          Rect2D parentRect,
                                          Extent2D figExtent);

struct SizeBarLayout {
    Rect2Df box;            ///< frame rect (== content when !frameon)
    Rect2Df bar;            ///< bar rect
    Point2D labelBaseline;  ///< left end of the label baseline
    bool fill = true;       ///< bar filled vs. stroked outline
    bool valid = false;     ///< false when size <= 0 or label empty path
};

/// Text metrics needed by `layoutSizeBar` (matches TextRenderer's
/// TextMetrics / text::TextMeasure shape) — declared above with the
/// anchored-artist structs.

/// Lay out a SizeBar inside `axesRect` (figure pixels, Y-down).
/// `measure` returns {width, height, ascent} for the label at scale
/// `bar.fontSize` (matches TextRenderer::measureText / VectorRenderer).
[[nodiscard]] SizeBarLayout
layoutSizeBar(const SizeBar& bar, const Axes& axes, Rect2D axesRect,
              Extent2D figExtent, float dpi,
              const std::function<SizeBarTextMeasure(std::string_view,
                                                   float)>& measure);

/// Convert a position from a coordinate system to display (pixel) coordinates.
/// `axesRect` is the pixel rect of the axes, `figExtent` is the full framebuffer,
/// `axes` provides the scale/projection-aware data mapping, `dpi` is the figure DPI.
Point2D toDisplay(float x, float y, CoordSystem coords,
                  Rect2D axesRect, Extent2D figExtent,
                  const Axes& axes, float dpi = 100.0f,
                  float xyOffsetX = 0.0f, float xyOffsetY = 0.0f);

/// Compute the pixel position of text given alignment and text metrics.
/// `metrics` = {width, height, ascent} from TextRenderer::measureText.
Point2D alignText(Point2D pos, HAlign ha, VAlign va,
                  float width, float height, float ascent);

} // namespace volcano::plot
