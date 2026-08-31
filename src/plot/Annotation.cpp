// volcano/plot/Annotation.cpp — annotation coordinate transforms
#include "volcano/plot/Annotation.hpp"

#include <cmath>

namespace volcano::plot {

Point2D toDisplay(float x, float y, CoordSystem coords,
                  Rect2D axesRect, Extent2D figExtent,
                  const Viewport& viewport, float dpi,
                  float xyOffsetX, float xyOffsetY) {
    Point2D result{0.0f, 0.0f};
    switch (coords) {
        case CoordSystem::Data: {
            // Data → pixel: x maps to axesRect.x + fraction * width
            // y maps to axesRect.y + (1 - fraction) * height (Y-up → Y-down)
            float fx = (x - viewport.x.min) / viewport.x.span();
            float fy = (y - viewport.y.min) / viewport.y.span();
            result.x = axesRect.x + fx * axesRect.width;
            result.y = axesRect.y + (1.0f - fy) * axesRect.height;
            break;
        }
        case CoordSystem::Axes: {
            // (0,0) = bottom-left, (1,1) = top-right of axes rect
            result.x = axesRect.x + x * axesRect.width;
            result.y = axesRect.y + (1.0f - y) * axesRect.height;
            break;
        }
        case CoordSystem::Figure: {
            // (0,0) = bottom-left, (1,1) = top-right of figure
            result.x = x * figExtent.width;
            result.y = (1.0f - y) * figExtent.height;
            break;
        }
        case CoordSystem::Display: {
            // Already in pixel coordinates (Y-down)
            result.x = x;
            result.y = y;
            break;
        }
        case CoordSystem::OffsetPoints: {
            // First convert (x, y) from data to pixel, then apply offset.
            float fx = (x - viewport.x.min) / viewport.x.span();
            float fy = (y - viewport.y.min) / viewport.y.span();
            result.x = axesRect.x + fx * axesRect.width;
            result.y = axesRect.y + (1.0f - fy) * axesRect.height;
            // Offset in points: 1 point = dpi/72 pixels.
            // Positive x = right, positive y = up (so subtract from y for Y-down).
            float ptScale = dpi / 72.0f;
            result.x += xyOffsetX * ptScale;
            result.y -= xyOffsetY * ptScale;
            break;
        }
    }
    return result;
}

Point2D alignText(Point2D pos, HAlign ha, VAlign va,
                  float width, float height, float ascent) {
    Point2D result = pos;
    // Horizontal alignment: adjust x so the text is aligned correctly.
    // The text renderer draws from (x, y) as the baseline-left origin.
    switch (ha) {
        case HAlign::Left:   break;  // x is already the left edge
        case HAlign::Center: result.x -= width * 0.5f; break;
        case HAlign::Right:  result.x -= width; break;
    }
    // Vertical alignment: adjust y so the text is aligned correctly.
    // The text renderer's (x, y) is the baseline.
    //   text top    = y - ascent
    //   text bottom = y - ascent + height = y + descent
    //   vertical center = y - ascent + height/2
    switch (va) {
        case VAlign::Baseline: break;  // y is already the baseline
        case VAlign::Top:      result.y += ascent; break;
        case VAlign::Center:   result.y += ascent - height * 0.5f; break;
        case VAlign::Bottom:   result.y += ascent - height; break;
    }
    return result;
}

} // namespace volcano::plot
