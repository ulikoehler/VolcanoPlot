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

    /// Whether to clip the text to the axes rect (matplotlib clip_on).
    /// Default false (matplotlib default); set true to clip data-space text.
    bool clipOn = false;
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

    /// Shrink the arrow on both ends by this many pixels
    /// (so it doesn't overlap the text or the data point marker).
    float shrinkA = 2.0f;  ///< shrink at the text end
    float shrinkB = 2.0f;  ///< shrink at the data point end
};

class Axes;

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
