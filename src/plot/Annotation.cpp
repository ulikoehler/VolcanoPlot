// volcano/plot/Annotation.cpp — annotation coordinate transforms
#include "volcano/plot/Annotation.hpp"
#include "volcano/plot/Axes.hpp"

#include <algorithm>
#include <array>
#include <tuple>
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


// ─── mpl bezier.py ports (quadratic Bézier helpers) ───────────────────────

using QuadBez = std::array<Point2D, 3>;

Point2D quadAt(const QuadBez& b, float t) {
    float u = 1.0f - t;
    return {u*u*b[0].x + 2*u*t*b[1].x + t*t*b[2].x,
            u*u*b[0].y + 2*u*t*b[1].y + t*t*b[2].y};
}

// mpl get_cos_sin
std::pair<float,float> getCosSin(float x0, float y0, float x1, float y1) {
    float d = std::hypot(x1 - x0, y1 - y0);
    if (d == 0.0f) return {0.0f, 0.0f};
    return {(x1 - x0) / d, (y1 - y0) / d};
}

// mpl get_normal_points → {left, right}
std::pair<Point2D,Point2D> getNormalPoints(float cx, float cy,
                                         float cosT, float sinT,
                                         float length) {
    if (length == 0.0f) return {{cx, cy}, {cx, cy}};
    return {{cx + length * sinT, cy - length * cosT},
            {cx - length * sinT, cy + length * cosT}};
}

// mpl find_control_points: quad bezier through c1 (t=0), mm (t=0.5), c2 (t=1)
QuadBez findControlPoints(Point2D c1, Point2D mm, Point2D c2) {
    return {c1, {0.5f * (4*mm.x - (c1.x + c2.x)),
                 0.5f * (4*mm.y - (c1.y + c2.y))}, c2};
}

// mpl make_wedged_bezier2 → {left, right} quad beziers.
std::pair<QuadBez,QuadBez> makeWedgedBezier2(const QuadBez& b, float width,
                                           float w1 = 1.0f, float wm = 0.5f,
                                           float w2 = 0.0f) {
    auto [c1, cm, c3] = std::tie(b[0], b[1], b[2]);
    auto [ct1, st1] = getCosSin(c1.x, c1.y, cm.x, cm.y);
    auto [ct2, st2] = getCosSin(cm.x, cm.y, c3.x, c3.y);
    auto [c1l, c1r] = getNormalPoints(c1.x, c1.y, ct1, st1, width * w1);
    auto [c3l, c3r] = getNormalPoints(c3.x, c3.y, ct2, st2, width * w2);
    Point2D c12{(c1.x + cm.x) * .5f, (c1.y + cm.y) * .5f};
    Point2D c23{(cm.x + c3.x) * .5f, (cm.y + c3.y) * .5f};
    Point2D c123{(c12.x + c23.x) * .5f, (c12.y + c23.y) * .5f};
    auto [ct123, st123] = getCosSin(c12.x, c12.y, c23.x, c23.y);
    auto [c123l, c123r] = getNormalPoints(c123.x, c123.y, ct123, st123,
                                          width * wm);
    return {findControlPoints(c1l, c123l, c3l),
            findControlPoints(c1r, c123r, c3r)};
}

// mpl get_parallels → {left, right} quad beziers.
std::pair<QuadBez,QuadBez> getParallels(const QuadBez& b, float width) {
    auto [c1, cm, c2] = std::tie(b[0], b[1], b[2]);
    float ct1, st1, ct2, st2;
    // check_if_parallel on (c1-cm) and (cm-c2)
    float t1 = std::atan2(c1.x - cm.x, c1.y - cm.y);
    float t2 = std::atan2(cm.x - c2.x, cm.y - c2.y);
    float dt = std::abs(t1 - t2);
    bool antiParallel = std::abs(dt - float(M_PI)) < 1e-5f;
    if (antiParallel) {
        std::tie(ct1, st1) = getCosSin(c1.x, c1.y, c2.x, c2.y);
        ct2 = ct1; st2 = st1;
    } else {
        std::tie(ct1, st1) = getCosSin(c1.x, c1.y, cm.x, cm.y);
        std::tie(ct2, st2) = getCosSin(cm.x, cm.y, c2.x, c2.y);
    }
    auto [c1l, c1r] = getNormalPoints(c1.x, c1.y, ct1, st1, width);
    auto [c2l, c2r] = getNormalPoints(c2.x, c2.y, ct2, st2, width);
    // mpl get_intersection; falls back to midpoint on near-parallel lines.
    auto intersect = [](float cx1, float cy1, float c1t, float s1t,
                        float cx2, float cy2, float c2t, float s2t,
                        Point2D fb) {
        float a = s1t, b_ = -c1t, c = s2t, d = -c2t;
        float ad_bc = a * d - b_ * c;
        if (std::abs(ad_bc) < 1e-12f) return fb;
        float r1 = s1t * cx1 - c1t * cy1;
        float r2 = s2t * cx2 - c2t * cy2;
        return Point2D{(d * r1 - b_ * r2) / ad_bc,
                       (-c * r1 + a * r2) / ad_bc};
    };
    Point2D cml = intersect(c1l.x, c1l.y, ct1, st1, c2l.x, c2l.y, ct2, st2,
                            {0.5f*(c1l.x + c2l.x), 0.5f*(c1l.y + c2l.y)});
    Point2D cmr = intersect(c1r.x, c1r.y, ct1, st1, c2r.x, c2r.y, ct2, st2,
                            {0.5f*(c1r.x + c2r.x), 0.5f*(c1r.y + c2r.y)});
    return {{c1l, cml, c2l}, {c1r, cmr, c2r}};
}

// mpl split_de_casteljau for a quad bezier.
std::pair<QuadBez,QuadBez> splitQuad(const QuadBez& b, float t) {
    Point2D a{b[0].x + (b[1].x - b[0].x) * t, b[0].y + (b[1].y - b[0].y) * t};
    Point2D c{b[1].x + (b[2].x - b[1].x) * t, b[1].y + (b[2].y - b[1].y) * t};
    Point2D m{a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t};
    return {{b[0], a, m}, {m, c, b[2]}};
}

// mpl split_bezier_intersecting_with_closedpath for a circle.
// Returns {left, right}; nullopt when both ends are on the same side.
std::optional<std::pair<QuadBez,QuadBez>>
splitQuadAtCircle(const QuadBez& b, Point2D cc, float r) {
    float r2 = r * r;
    auto inside = [&](Point2D p) {
        float dx = p.x - cc.x, dy = p.y - cc.y;
        return dx*dx + dy*dy < r2;
    };
    float t0 = 0.0f, t1 = 1.0f;
    Point2D start = quadAt(b, t0), end = quadAt(b, t1);
    bool sIn = inside(start), eIn = inside(end);
    if (sIn == eIn && (start.x != end.x || start.y != end.y))
        return std::nullopt;
    const float tol = 0.01f;
    while (std::hypot(start.x - end.x, start.y - end.y) >= tol) {
        float tm = 0.5f * (t0 + t1);
        Point2D mid = quadAt(b, tm);
        if (sIn != inside(mid)) {
            t1 = tm;
            if (end.x == mid.x && end.y == mid.y) break;
            end = mid;
        } else {
            t0 = tm;
            if (start.x == mid.x && start.y == mid.y) break;
            start = mid; sIn = inside(mid);
        }
    }
    return splitQuad(b, 0.5f * (t0 + t1));
}

// mpl _point_along_a_line: point at distance d from (x0,y0) toward (x1,y1)
Point2D pointAlongLine(Point2D p0, Point2D p1, float d) {
    float dx = p0.x - p1.x, dy = p0.y - p1.y;
    float ff = d / std::hypot(dx, dy);
    return {p0.x - ff * dx, p0.y - ff * dy};
}

// Fit a quadratic bezier to the sampled connection path (exact when the
// underlying connection is itself quadratic, e.g. mpl arc3).
QuadBez quadFit(std::span<const Point2D> path) {
    Point2D c0 = path.front(), c2 = path.back();
    Point2D mid = path[path.size() / 2];   // ≈ B(0.5)
    return {c0, {2*mid.x - 0.5f*(c0.x + c2.x),
                 2*mid.y - 0.5f*(c0.y + c2.y)}, c2};
}

// Flattened mpl arrow body built from quad-bézier control points.
std::vector<Point2D> bezierBodyOutline(const QuadBez& arrow,
                                       const ArrowStyleSpec& spec,
                                       float ms) {
    Path p;
    auto c3 = [](Path& q, const QuadBez& b) { q.curve3(b[1], b[2]); };
    if (spec.body == ArrowStyleSpec::Body::Wedge) {
        auto [bl, br] = makeWedgedBezier2(arrow, spec.tailWidth * ms / 2.0f,
                                          1.0f, spec.shrinkFactor, 0.0f);
        p.moveTo(bl[0]); c3(p, bl);
        p.lineTo(br[2]); c3(p, QuadBez{br[2], br[1], br[0]});
        p.close();
    } else if (spec.body == ArrowStyleSpec::Body::Simple) {
        float hl = spec.headLength * ms;
        auto split = splitQuadAtCircle(arrow, arrow[2], hl);
        QuadBez head;
        std::optional<QuadBez> tail;
        if (split) { tail = split->first; head = split->second; }
        else {
            // Straight-line fallback (mpl NonIntersectingPathException).
            Point2D h0 = pointAlongLine(arrow[2], arrow[1], hl);
            head = {h0, {0.5f*(h0.x + arrow[2].x), 0.5f*(h0.y + arrow[2].y)},
                    arrow[2]};
        }
        auto [hl_, hr_] = makeWedgedBezier2(head, spec.headWidth * ms / 2.0f,
                                            1.0f, 0.5f, 0.0f);
        if (tail) {
            auto [tl, tr] = getParallels(*tail, spec.tailWidth * ms / 2.0f);
            p.moveTo(tr[0]); c3(p, tr);
            p.lineTo(hr_[0]); c3(p, hr_);
            p.curve3(hl_[1], hl_[0]);
            p.lineTo(tl[2]); c3(p, QuadBez{tl[2], tl[1], tl[0]});
            p.close();
        } else {
            p.moveTo(hr_[0]); c3(p, hr_);
            p.curve3(hl_[1], hl_[0]);
            p.close();
        }
    } else {  // Fancy
        float hl = spec.headLength * ms;
        auto split = splitQuadAtCircle(arrow, arrow[2], hl);
        QuadBez head;
        if (split) head = split->second;
        else {
            Point2D h0 = pointAlongLine(arrow[2], arrow[1], hl);
            head = {h0, {0.5f*(h0.x + arrow[2].x), 0.5f*(h0.y + arrow[2].y)},
                    arrow[2]};
        }
        auto split2 = splitQuadAtCircle(arrow, arrow[2], hl * 0.8f);
        QuadBez tail = split2 ? split2->first
                              : QuadBez{arrow[0], arrow[0], arrow[0]};
        auto [hl_, hr_] = makeWedgedBezier2(head, spec.headWidth * ms / 2.0f,
                                            1.0f, 0.6f, 0.0f);
        auto [tl, tr] = makeWedgedBezier2(tail, spec.tailWidth * ms * 0.5f,
                                        1.0f, 0.6f, 0.3f);
        auto split3 = splitQuadAtCircle(arrow, arrow[0],
                                        spec.tailWidth * ms * 0.3f);
        Point2D tailStart = split3 ? split3->first[2] : arrow[0];
        p.moveTo(tailStart);
        p.lineTo(tr[0]); c3(p, tr);
        p.lineTo(hr_[0]); c3(p, hr_);
        p.curve3(hl_[1], hl_[0]);
        p.lineTo(tl[2]); c3(p, QuadBez{tl[2], tl[1], tl[0]});
        p.lineTo(tailStart);
        p.close();
    }
    auto pts = p.flatten(24);
    return pts;
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

    // Named full-body styles: mpl Simple/Fancy/Wedge transmute — one
    // filled bezier outline swept along the connection path.
    if (spec.body != ArrowStyleSpec::Body::None) {
        g.fills.push_back(bezierBodyOutline(quadFit(path), spec, ms));
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

// mpl legend/anchored-artist loc → axes anchor fraction (fx,fy, y-up)
// plus the box's own anchor point (bx,by).
struct AnchorLoc { float fx, fy, bx, by; };

static AnchorLoc anchorForLoc(std::string_view loc) {
    if (loc == "upper left" || loc == "2")   return {0, 1, 0, 1};
    if (loc == "lower left" || loc == "3")   return {0, 0, 0, 0};
    if (loc == "lower right" || loc == "4")  return {1, 0, 1, 0};
    if (loc == "center left" || loc == "6")  return {0, 0.5f, 0, 0.5f};
    if (loc == "right" || loc == "center right" || loc == "5" ||
        loc == "7")                          return {1, 0.5f, 1, 0.5f};
    if (loc == "lower center" || loc == "8") return {0.5f, 0, 0.5f, 0};
    if (loc == "upper center" || loc == "9") return {0.5f, 1, 0.5f, 1};
    if (loc == "center" || loc == "10")      return {0.5f, 0.5f, 0.5f, 0.5f};
    return {1, 1, 1, 1};  // "upper right" / "1" / unknown
}

// Position a cw×ch px box at anchor `a` inside axesRect, `bp` px from
// the axes edge (y-down pixel space).
static Rect2Df anchoredBox(AnchorLoc a, float cw, float ch,
                           Rect2D axesRect, float bp) {
    float px = axesRect.x + a.fx * float(axesRect.width);
    float py = axesRect.y + (1.0f - a.fy) * float(axesRect.height);
    if (a.bx > 0.5f) px -= bp; else if (a.bx < 0.5f) px += bp;
    if (a.by > 0.5f) py += bp; else if (a.by < 0.5f) py -= bp;
    return {px - a.bx * cw, py - (1.0f - a.by) * ch, cw, ch};
}

AnchoredTextLayout layoutAnchoredText(
    const AnchoredText& at, const Axes& /*axes*/, Rect2D axesRect,
    const std::function<SizeBarTextMeasure(std::string_view,
                                           float)>& measure) {
    AnchoredTextLayout out;
    if (at.text.empty()) return out;
    const float fontPx = 16.0f * at.fontSize;
    // Split lines; measure each.
    float maxW = 0, firstAscent = 0, lineH = 0;
    size_t start = 0;
    while (true) {
        size_t nl = at.text.find('\n', start);
        auto line = std::string_view(at.text).substr(
            start, nl == std::string_view::npos
                       ? std::string_view::npos : nl - start);
        auto m = measure(line, at.fontSize);
        maxW = std::max(maxW, m.width);
        if (lineH == 0) { firstAscent = m.ascent; lineH = m.height * 1.25f; }
        out.lineBaselines.push_back({});
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    const float pad = at.pad * fontPx;
    const float contentH = firstAscent + lineH *
                           (out.lineBaselines.size() - 1) +
                           (lineH / 1.25f - firstAscent);
    float boxW = maxW + 2 * pad;
    float boxH = contentH + 2 * pad;
    out.box = anchoredBox(anchorForLoc(at.loc), boxW, boxH, axesRect,
                          at.borderpad * fontPx);
    for (size_t i = 0; i < out.lineBaselines.size(); ++i)
        out.lineBaselines[i] = {out.box.x + pad,
                                out.box.y + pad + firstAscent +
                                    float(i) * lineH};
    out.valid = true;
    return out;
}

SizeBarLayout layoutSizeBar(
    const SizeBar& bar, const Axes& axes, Rect2D axesRect,
    Extent2D /*figExtent*/, float dpi,
    const std::function<SizeBarTextMeasure(std::string_view, float)>& measure) {
    SizeBarLayout out;
    if (bar.size <= 0.0f) return out;

    const float fontPx = 16.0f * bar.fontSize;
    const auto& vp = axes.viewport();

    // Bar extent: data units → axes-fraction → px (mpl evaluates the
    // rectangle through transData at x=0; fall back to the viewport min
    // when 0 is out of domain, e.g. log scales).
    auto fracLenX = [&](float d) {
        float a = axes.dataToFraction({0.0f, 0.0f}).x;
        float b = axes.dataToFraction({d, 0.0f}).x;
        if (!std::isfinite(b - a)) {
            a = axes.dataToFraction({vp.x.min, 0.0f}).x;
            b = axes.dataToFraction({vp.x.min + d, 0.0f}).x;
        }
        return std::fabs(b - a) * float(axesRect.width);
    };
    auto fracLenY = [&](float d) {
        float a = axes.dataToFraction({0.0f, 0.0f}).y;
        float b = axes.dataToFraction({0.0f, d}).y;
        if (!std::isfinite(b - a)) {
            a = axes.dataToFraction({0.0f, vp.y.min}).y;
            b = axes.dataToFraction({0.0f, vp.y.min + d}).y;
        }
        return std::fabs(b - a) * float(axesRect.height);
    };

    const float barW = fracLenX(bar.size);
    // mpl: size_vertical=0 → a stroked 0-height rect ≈ a thin line.
    const float lineH = 0.8f * dpi / 72.0f;
    const float barH = bar.sizeVertical > 0.0f
                           ? fracLenY(bar.sizeVertical) : lineH;
    const bool fill = bar.fillBar.value_or(bar.sizeVertical > 0.0f) ||
                      bar.sizeVertical <= 0.0f;

    auto lm = measure(bar.label, bar.fontSize);
    const float labelW = bar.label.empty() ? 0.0f : lm.width;
    const float labelH = bar.label.empty() ? 0.0f : lm.height;
    const float sepPx = bar.label.empty() ? 0.0f : bar.sep * dpi / 72.0f;

    const float contentW = std::max(barW, labelW);
    const float contentH = barH + sepPx + labelH;
    const float pad = bar.pad * fontPx;
    const float boxW = contentW + 2.0f * pad;
    const float boxH = contentH + 2.0f * pad;

    const float bp = bar.borderpad * fontPx;
    out.box = anchoredBox(anchorForLoc(bar.loc), boxW, boxH,
                          axesRect, bp);
    const float boxX = out.box.x, boxY = out.box.y;

    // Content: bar centered horizontally; label under (or over) it.
    const float cx = boxX + pad + contentW * 0.5f;
    float barY, labelBaseline;
    if (bar.labelTop) {
        labelBaseline = boxY + pad + lm.ascent;
        barY = labelBaseline + (lm.height - lm.ascent) + sepPx;
    } else {
        barY = boxY + pad;
        labelBaseline = barY + barH + sepPx + lm.ascent;
    }
    out.bar = {cx - barW * 0.5f, barY, barW, barH};
    out.labelBaseline = {cx - labelW * 0.5f, labelBaseline};
    out.fill = fill;
    out.valid = true;
    return out;
}


InsetIndicatorLayout layoutInsetIndicator(const InsetIndicator& ind,
                                          const Axes& parent,
                                          Rect2D parentRect,
                                          Extent2D figExtent) {
    InsetIndicatorLayout out;
    // Rectangle in parent data coords: explicit bounds or the inset's
    // viewport limits (mpl: bounds default = inset_ax data limits).
    float dx0 = ind.x0, dy0 = ind.y0, dx1 = ind.x1, dy1 = ind.y1;
    if (!ind.hasBounds) {
        if (!ind.inset) return out;
        const auto& vp = ind.inset->viewport();
        dx0 = vp.x.min; dx1 = vp.x.max;
        dy0 = vp.y.min; dy1 = vp.y.max;
    }
    auto toPx = [&](float dx, float dy) {
        auto f = parent.dataToFraction({dx, dy});
        return Point2D{float(parentRect.x) + f.x * float(parentRect.width),
                       float(parentRect.y) +
                           (1.0f - f.y) * float(parentRect.height)};
    };
    Point2D p00 = toPx(dx0, dy0), p11 = toPx(dx1, dy1);
    if (!std::isfinite(p00.x) || !std::isfinite(p00.y) ||
        !std::isfinite(p11.x) || !std::isfinite(p11.y))
        return out;
    float rx0 = std::min(p00.x, p11.x), rx1 = std::max(p00.x, p11.x);
    float ry0 = std::min(p00.y, p11.y), ry1 = std::max(p00.y, p11.y);
    out.rect = {rx0, ry0, rx1 - rx0, ry1 - ry0};
    // Rect corners in px, labeled geometrically: LL, UL, LR, UR.
    const Point2D rc[4] = {{rx0, ry1}, {rx0, ry0}, {rx1, ry1}, {rx1, ry0}};

    std::array<bool, 4> vis{false, false, false, false};
    Point2D ic[4]{};
    if (ind.inset) {
        const auto& ir = ind.inset->rect;
        ic[0] = {float(ir.x), float(ir.y) + float(ir.height)};   // LL
        ic[1] = {float(ir.x), float(ir.y)};                       // UL
        ic[2] = {float(ir.x) + float(ir.width),
                 float(ir.y) + float(ir.height)};                 // LR
        ic[3] = {float(ir.x) + float(ir.width), float(ir.y)};     // UR
        if (ind.connectors) {
            vis = *ind.connectors;
        } else {
            // mpl auto visibility: compare the indicator rect against
            // the inset axes bbox in figure space (y-up).
            float H = float(figExtent.height);
            float bx0 = float(ir.x), bx1 = float(ir.x + ir.width);
            float by0 = H - float(ir.y + ir.height);
            float by1 = H - float(ir.y);
            float rby0 = H - ry1, rby1 = H - ry0;
            bool x0 = rx0 < bx0, x1 = rx1 < bx1;
            bool y0 = rby0 < by0, y1 = rby1 < by1;
            vis = {bool(x0 ^ y0), bool(x0 == y1),
                   bool(x1 == y0), bool(x1 ^ y1)};
        }
        for (int i = 0; i < 4; ++i)
            out.connectors[i] = {rc[i], ic[i]};
    }
    out.connVisible = vis;
    out.valid = true;
    return out;
}

} // namespace volcano::plot
