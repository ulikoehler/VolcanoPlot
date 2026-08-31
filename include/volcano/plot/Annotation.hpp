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

#include "volcano/plot/Types.hpp"

#include <string>
#include <vector>

namespace volcano::plot {

/// Coordinate system for text/annotation positioning.
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

/// Horizontal alignment.
enum class HAlign { Left, Center, Right };

/// Vertical alignment.
enum class VAlign { Bottom, Center, Top, Baseline };

/// Arrow style for annotations.
enum class ArrowStyle {
    None,       ///< No arrow (just a line).
    Simple,     ///< Simple line with arrowhead.
    Fancy,      ///< FancyArrowPatch-style with curved shaft.
    Wedge,      ///< Wedge-shaped arrowhead.
    Arrow,      ///< Standard arrow (matplotlib '->').
    ArrowSmall, ///< Small arrow (matplotlib '->' with small head).
};

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
    /// Arrow color.
    Color arrowColor = Color::black();
    /// Arrow line width in pixels.
    float arrowWidth = 1.0f;
    /// Arrowhead size in pixels (length of the arrowhead).
    float arrowHeadSize = 10.0f;
    /// Arrowhead opening angle in degrees.
    float arrowHeadAngle = 30.0f;

    /// Optional background box for the text.
    Color bboxFaceColor = Color::transparent();
    Color bboxEdgeColor = Color::transparent();
    float bboxPadding = 4.0f;

    /// Shrink the arrow on both ends by this many pixels
    /// (so it doesn't overlap the text or the data point marker).
    float shrinkA = 2.0f;  ///< shrink at the text end
    float shrinkB = 2.0f;  ///< shrink at the data point end
};

/// Convert a position from a coordinate system to display (pixel) coordinates.
/// `axesRect` is the pixel rect of the axes, `figExtent` is the full framebuffer,
/// `viewport` is the data viewport, `dpi` is the figure DPI.
Point2D toDisplay(float x, float y, CoordSystem coords,
                  Rect2D axesRect, Extent2D figExtent,
                  const Viewport& viewport, float dpi = 100.0f,
                  float xyOffsetX = 0.0f, float xyOffsetY = 0.0f);

/// Compute the pixel position of text given alignment and text metrics.
/// `metrics` = {width, height, ascent} from TextRenderer::measureText.
Point2D alignText(Point2D pos, HAlign ha, VAlign va,
                  float width, float height, float ascent);

} // namespace volcano::plot
