// volcano/plot/Annotation.cpp — annotation coordinate transforms
#include "volcano/plot/Annotation.hpp"
#include "volcano/plot/Axes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace volcano::plot {

Point2D toDisplay(float x, float y, CoordSystem coords,
                  Rect2D axesRect, Extent2D figExtent,
                  const Axes& axes, float dpi,
                  float xyOffsetX, float xyOffsetY) {
    Point2D result{0.0f, 0.0f};
    switch (coords) {
        case CoordSystem::Data: {
            // Data → pixel: x maps to axesRect.x + fraction * width
            // y maps to axesRect.y + (1 - fraction) * height (Y-up → Y-down)
            Point2D f = axes.dataToFraction({x, y});
            result.x = axesRect.x + f.x * axesRect.width;
            result.y = axesRect.y + (1.0f - f.y) * axesRect.height;
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
            Point2D f = axes.dataToFraction({x, y});
            result.x = axesRect.x + f.x * axesRect.width;
            result.y = axesRect.y + (1.0f - f.y) * axesRect.height;
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

ConnectionStyle parseConnectionStyle(std::string_view s) {
    ConnectionStyle cs;
    auto comma = s.find(',');
    std::string_view name = s.substr(0, comma);
    if (name == "arc3")        cs.kind = ConnectionStyle::Kind::Arc3;
    else if (name == "arc")    cs.kind = ConnectionStyle::Kind::Arc;
    else if (name == "angle")  cs.kind = ConnectionStyle::Kind::Angle;
    else if (name == "bar")    cs.kind = ConnectionStyle::Kind::Bar;
    else return cs;  // unknown → straight arc3

    // Parse "key=value" pairs.
    while (comma != std::string_view::npos) {
        size_t next = s.find(',', comma + 1);
        auto kv = s.substr(comma + 1, next == std::string_view::npos
                                          ? next : next - comma - 1);
        if (auto eq = kv.find('='); eq != std::string_view::npos) {
            auto key = kv.substr(0, eq);
            float val = 0.0f;
            std::from_chars(kv.data() + eq + 1, kv.data() + kv.size(), val);
            if (key == "rad")           cs.rad = val;
            else if (key == "angleA")   cs.angleA = val;
            else if (key == "angleB")   cs.angleB = val;
            else if (key == "fraction") cs.fraction = val;
        }
        comma = next;
    }
    return cs;
}

namespace {

/// Evaluate a quadratic bezier A→C→B at t.
Point2D quadBez(Point2D a, Point2D c, Point2D b, float t) {
    float u = 1.0f - t;
    return {u * u * a.x + 2.0f * u * t * c.x + t * t * b.x,
            u * u * a.y + 2.0f * u * t * c.y + t * t * b.y};
}

/// Direction unit vector for a mathematical angle (degrees, CCW, Y-up)
/// expressed in screen pixels (Y-down).
Point2D angleDir(float deg) {
    float r = deg * static_cast<float>(M_PI) / 180.0f;
    return {std::cos(r), -std::sin(r)};
}

/// Intersect line (p, dir u) with line (q, dir v). Returns false if parallel.
bool lineIntersect(Point2D p, Point2D u, Point2D q, Point2D v, Point2D& out) {
    float det = u.x * v.y - u.y * v.x;
    if (std::fabs(det) < 1e-6f) return false;
    float t = ((q.x - p.x) * v.y - (q.y - p.y) * v.x) / det;
    out = {p.x + u.x * t, p.y + u.y * t};
    return true;
}

/// Sample a quadratic bezier into a polyline, appending to `out`.
void sampleQuad(std::vector<Point2D>& out, Point2D a, Point2D c, Point2D b,
                int samples) {
    for (int i = 1; i <= samples; ++i)
        out.push_back(quadBez(a, c, b, float(i) / samples));
}

} // namespace

std::vector<Point2D> connectionPath(Point2D a, Point2D b,
                                    const ConnectionStyle& cs,
                                    float shrinkA, float shrinkB,
                                    int samples) {
    std::vector<Point2D> path;
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return {a, b};
    // Unit perpendicular to AB (screen space, Y-down).
    Point2D perp{dy / len, -dx / len};

    switch (cs.kind) {
    case ConnectionStyle::Kind::Arc3: {
        if (std::fabs(cs.rad) < 1e-6f) { path = {a, b}; break; }
        // Control point: midpoint + rad * perpendicular * |AB|.
        Point2D mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        Point2D c{mid.x + perp.x * cs.rad * len,
                  mid.y + perp.y * cs.rad * len};
        path.push_back(a);
        sampleQuad(path, a, c, b, samples);
        break;
    }
    case ConnectionStyle::Kind::Angle:
    case ConnectionStyle::Kind::Arc: {
        // Two rays: from A along angleA, through B along angleB.
        Point2D u = angleDir(cs.angleA);
        Point2D v = angleDir(cs.angleB);
        Point2D kink;
        if (!lineIntersect(a, u, b, v, kink)) { path = {a, b}; break; }
        float dA = std::hypot(kink.x - a.x, kink.y - a.y);
        float dB = std::hypot(b.x - kink.x, b.y - kink.y);
        float rad = cs.rad;
        if (cs.kind == ConnectionStyle::Kind::Arc && rad <= 0.0f)
            rad = std::min(dA, dB) * 0.3f;  // arc always rounds
        if (rad > 0.0f && dA > 1e-3f && dB > 1e-3f) {
            // Round the corner: tangent points `rad` px from the kink
            // (clamped to the segment lengths), joined by a quad bezier.
            float r = std::min({rad, dA * 0.5f, dB * 0.5f});
            Point2D t1{kink.x - u.x * r, kink.y - u.y * r};
            // The kink→B segment has direction (b - kink)/dB.
            Point2D vb{(b.x - kink.x) / dB, (b.y - kink.y) / dB};
            Point2D t2{kink.x + vb.x * r, kink.y + vb.y * r};
            path.push_back(a);
            path.push_back(t1);
            sampleQuad(path, t1, kink, t2, samples / 2 + 2);
            path.push_back(b);
        } else {
            path = {a, kink, b};
        }
        break;
    }
    case ConnectionStyle::Kind::Bar: {
        // Bracket: A → A+perp·f·L → B+perp·f·L → B.
        float hgt = cs.fraction * len;
        path = {a,
                {a.x + perp.x * hgt, a.y + perp.y * hgt},
                {b.x + perp.x * hgt, b.y + perp.y * hgt},
                b};
        break;
    }
    }

    // Apply shrink along the local end tangents.
    if (shrinkA > 0.0f && path.size() >= 2) {
        Point2D d{path[1].x - path[0].x, path[1].y - path[0].y};
        float l = std::hypot(d.x, d.y);
        if (l > 1e-4f)
            path[0] = {path[0].x + d.x / l * shrinkA,
                       path[0].y + d.y / l * shrinkA};
    }
    if (shrinkB > 0.0f && path.size() >= 2) {
        size_t n = path.size();
        Point2D d{path[n-1].x - path[n-2].x, path[n-1].y - path[n-2].y};
        float l = std::hypot(d.x, d.y);
        if (l > 1e-4f)
            path[n-1] = {path[n-1].x - d.x / l * shrinkB,
                         path[n-1].y - d.y / l * shrinkB};
    }
    return path;
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
