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

namespace {

std::string_view trim(std::string_view v) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t'; };
    while (!v.empty() && isSpace(v.front())) v.remove_prefix(1);
    while (!v.empty() && isSpace(v.back())) v.remove_suffix(1);
    return v;
}

} // namespace

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
            auto key = trim(kv.substr(0, eq));
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

// ─── arrowstyle ───────────────────────────────────────────────────────────

ArrowStyleSpec parseArrowStyle(std::string_view s) {
    using End = ArrowStyleSpec::End;
    using Body = ArrowStyleSpec::Body;
    ArrowStyleSpec spec;
    auto comma = s.find(',');
    std::string_view name = trim(s.substr(0, comma));

    // mpl _ArrowStyle._style_list shorthands (patches.py).
    if (name == "-")            { /* both None */ }
    else if (name == "<-")      spec.headA = End::Open;
    else if (name == "->")      spec.headB = End::Open;
    else if (name == "<->")     { spec.headA = spec.headB = End::Open; }
    else if (name == "-[")      spec.headB = End::Bracket;
    else if (name == "<-[")     { spec.headA = End::Open;
                                  spec.headB = End::Bracket; }
    else if (name == "]-[")     { spec.headA = spec.headB = End::Bracket; }
    else if (name == "-|>")     spec.headB = End::Filled;
    else if (name == "<|-")     spec.headA = End::Filled;
    else if (name == "<|-|>")   { spec.headA = spec.headB = End::Filled; }
    else if (name == "|-|")     { spec.headA = spec.headB = End::Bar; }
    else if (name == "simple") {
        spec.body = Body::Simple;
        spec.headLength = 0.5f; spec.headWidth = 0.5f;
        spec.tailWidth = 0.2f;
    }
    else if (name == "fancy") {
        spec.body = Body::Fancy;
        spec.headLength = 0.4f; spec.headWidth = 0.4f;
        spec.tailWidth = 0.4f;
    }
    else if (name == "wedge") {
        spec.body = Body::Wedge;
        spec.tailWidth = 0.3f;
    }
    else spec.headB = End::Open;  // unknown → "->"

    while (comma != std::string_view::npos) {
        size_t next = s.find(',', comma + 1);
        auto kv = s.substr(comma + 1, next == std::string_view::npos
                                          ? next : next - comma - 1);
        if (auto eq = kv.find('='); eq != std::string_view::npos) {
            auto key = trim(kv.substr(0, eq));
            float val = 0.0f;
            std::from_chars(kv.data() + eq + 1, kv.data() + kv.size(), val);
            if (key == "head_length")        spec.headLength = val;
            else if (key == "head_width")    spec.headWidth = val;
            else if (key == "tail_width")    spec.tailWidth = val;
            else if (key == "widthA")        spec.widthA = val;
            else if (key == "widthB")        spec.widthB = val;
            else if (key == "lengthA")       spec.lengthA = val;
            else if (key == "lengthB")       spec.lengthB = val;
            else if (key == "shrink_factor") spec.shrinkFactor = val;
            else if (key == "mutation_scale") spec.mutationSize = val;
        }
        comma = next;
    }
    return spec;
}

namespace {

/// Unit tangent at a path end and its left normal (screen Y-down).
struct EndFrame { Point2D u, n; };

EndFrame frameAt(std::span<const Point2D> path, bool atEnd) {
    Point2D d = atEnd
        ? Point2D{path.back().x - path[path.size() - 2].x,
                  path.back().y - path[path.size() - 2].y}
        : Point2D{path[0].x - path[1].x,
                  path[0].y - path[1].y};
    float l = std::hypot(d.x, d.y);
    if (l < 1e-6f) return {{1, 0}, {0, -1}};
    Point2D u{d.x / l, d.y / l};
    return {u, {-u.y, u.x}};
}

/// Walk the path from the end, returning the point `dist` back along it.
Point2D pointBackAlongPath(std::span<const Point2D> path, float dist) {
    Point2D cur = path.back();
    for (size_t i = path.size() - 1; i > 0; --i) {
        Point2D prev = path[i - 1];
        float seg = std::hypot(cur.x - prev.x, cur.y - prev.y);
        if (seg > dist) {
            float t = dist / seg;
            return {cur.x + (prev.x - cur.x) * t,
                    cur.y + (prev.y - cur.y) * t};
        }
        dist -= seg;
        cur = prev;
    }
    return cur;
}

/// Shorten the path by `dist` at the end (B side).
std::vector<Point2D> trimPathEnd(std::span<const Point2D> path, float dist) {
    std::vector<Point2D> out(path.begin(), path.end());
    while (out.size() > 1 && dist > 0.0f) {
        Point2D& last = out.back();
        Point2D prev = out[out.size() - 2];
        float seg = std::hypot(last.x - prev.x, last.y - prev.y);
        if (seg > dist + 1e-4f) {
            float t = dist / seg;
            last = {last.x + (prev.x - last.x) * t,
                    last.y + (prev.y - last.y) * t};
            break;
        }
        dist -= seg;
        out.pop_back();
    }
    return out;
}

} // namespace

ArrowGeometry buildArrowGeometry(std::span<const Point2D> path,
                                 const ArrowStyleSpec& spec,
                                 float lineWidth) {
    ArrowGeometry g;
    if (path.size() < 2) return g;
    float ms = spec.mutationSize;
    float hl = spec.headLength * ms;
    float hw = spec.headWidth * ms;

    // Named full-body styles: one filled polygon swept along the path.
    if (spec.body != ArrowStyleSpec::Body::None) {
        Point2D a = path.front(), b = path.back();
        auto f = frameAt(path, true);
        if (spec.body == ArrowStyleSpec::Body::Wedge) {
            // mpl Wedge: closed shape with half-width tail_width·ms/2 at
            // the start, ×shrink_factor at the middle, 0 at the tip.
            auto fa = frameAt(path, false);
            float h1 = spec.tailWidth * ms * 0.5f;
            float hm = h1 * spec.shrinkFactor;
            Point2D mid = path[path.size() / 2];
            g.fills.push_back({
                {a.x + fa.n.x * h1, a.y + fa.n.y * h1},
                {mid.x + fa.n.x * hm, mid.y + fa.n.y * hm},
                b,
                {mid.x - fa.n.x * hm, mid.y - fa.n.y * hm},
                {a.x - fa.n.x * h1, a.y - fa.n.y * h1}});
        } else {
            // Simple/Fancy: tail rectangle + head triangle along the path.
            float tw = spec.tailWidth * ms * 0.5f;
            Point2D headBase = pointBackAlongPath(path, hl);
            g.fills.push_back({
                {a.x + f.n.x * tw, a.y + f.n.y * tw},
                {headBase.x + f.n.x * (hw * 0.5f),
                 headBase.y + f.n.y * (hw * 0.5f)},
                b,
                {headBase.x - f.n.x * (hw * 0.5f),
                 headBase.y - f.n.y * (hw * 0.5f)},
                {a.x - f.n.x * tw, a.y - f.n.y * tw}});
        }
        return g;
    }

    // Shaft: trim back where heads attach (mpl pulls the line to the
    // head base so it doesn't poke through filled heads).
    auto shaft = std::vector<Point2D>(path.begin(), path.end());
    auto trimFor = [&](ArrowStyleSpec::End e, bool atEnd) {
        if (e == ArrowStyleSpec::End::Filled)
            shaft = atEnd ? trimPathEnd(shaft, hl * 0.5f) : shaft;
    };
    trimFor(spec.headB, true);
    if (!shaft.empty()) g.strokes.push_back(shaft);

    auto addEnd = [&](ArrowStyleSpec::End e, Point2D tip, EndFrame f,
                      float widthScale, float lengthScale) {
        // mpl pad_projected: heads overshoot the nominal tip by
        // 0.5·linewidth/sin(θ) so the projected cap covers it.
        float headDist = std::hypot(hl, hw);
        float sinT = headDist > 1e-6f ? hw / headDist : 0.0f;
        float pad = sinT > 1e-3f ? 0.5f * lineWidth / sinT : 0.0f;
        Point2D apex{tip.x + f.u.x * pad, tip.y + f.u.y * pad};
        switch (e) {
        case ArrowStyleSpec::End::Open:
            // mpl _Curve head: half-width = head_width·ms.
            g.strokes.push_back({{tip.x - f.u.x * hl + f.n.x * hw,
                                  tip.y - f.u.y * hl + f.n.y * hw},
                                 apex});
            g.strokes.push_back({{tip.x - f.u.x * hl - f.n.x * hw,
                                  tip.y - f.u.y * hl - f.n.y * hw},
                                 apex});
            break;
        case ArrowStyleSpec::End::Filled:
            g.fills.push_back({apex,
                {tip.x - f.u.x * hl + f.n.x * hw,
                 tip.y - f.u.y * hl + f.n.y * hw},
                {tip.x - f.u.x * hl - f.n.x * hw,
                 tip.y - f.u.y * hl - f.n.y * hw}});
            break;
        case ArrowStyleSpec::End::Bracket:
        case ArrowStyleSpec::End::Bar: {
            // mpl _get_bracket: stroked "[" — crossbar ±width·scale at
            // the tip plus stubs length·scale back along the path.
            float w = widthScale * ms;
            float l = lengthScale * ms;
            Point2D p1{tip.x + f.n.x * w, tip.y + f.n.y * w};
            Point2D p2{tip.x - f.n.x * w, tip.y - f.n.y * w};
            g.strokes.push_back({
                {p1.x - f.u.x * l, p1.y - f.u.y * l}, p1, p2,
                {p2.x - f.u.x * l, p2.y - f.u.y * l}});
            break;
        }
        case ArrowStyleSpec::End::None: break;
        }
    };
    if (spec.headA != ArrowStyleSpec::End::None)
        addEnd(spec.headA, path.front(), frameAt(path, false),
               spec.widthA, spec.lengthA);
    if (spec.headB != ArrowStyleSpec::End::None)
        addEnd(spec.headB, path.back(), frameAt(path, true),
               spec.widthB, spec.lengthB);
    return g;
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
