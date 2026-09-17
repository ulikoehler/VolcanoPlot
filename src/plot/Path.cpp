// volcano/plot/Path.cpp — Path flattening, bounds, triangulation, clipping
#include "volcano/plot/Path.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Point2D quadAt(Point2D p0, Point2D c, Point2D p1, float t) {
    float u = 1.0f - t;
    return {u * u * p0.x + 2 * u * t * c.x + t * t * p1.x,
            u * u * p0.y + 2 * u * t * c.y + t * t * p1.y};
}

Point2D cubicAt(Point2D p0, Point2D c1, Point2D c2, Point2D p1, float t) {
    float u = 1.0f - t;
    return {u*u*u*p0.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*p1.x,
            u*u*u*p0.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*p1.y};
}

} // namespace

// ─── flattening ───────────────────────────────────────────────────────────

std::vector<Path::Subpath> Path::toPolylines(int curveSteps) const {
    std::vector<Subpath> out;
    if (codes.empty()) {
        if (!vertices.empty()) out.push_back({vertices, false});
        return out;
    }
    Subpath cur;
    Point2D pen{0, 0};
    Point2D subStart{0, 0};
    size_t i = 0;
    while (i < codes.size()) {
        Code c = codes[i];
        Point2D v = vertices[i];
        switch (c) {
        case MoveTo:
            if (!cur.points.empty()) out.push_back(std::move(cur));
            cur = Subpath{};
            cur.points.push_back(v);
            pen = subStart = v;
            break;
        case LineTo:
            cur.points.push_back(v);
            pen = v;
            break;
        case Curve3:
            if (i + 1 < codes.size()) {
                Point2D ctrl = v, end = vertices[i + 1];
                for (int s = 1; s <= curveSteps; ++s)
                    cur.points.push_back(
                        quadAt(pen, ctrl, end, float(s) / curveSteps));
                pen = end;
                ++i;
            }
            break;
        case Curve4:
            if (i + 2 < codes.size()) {
                Point2D c1 = v, c2 = vertices[i + 1], end = vertices[i + 2];
                for (int s = 1; s <= curveSteps; ++s)
                    cur.points.push_back(
                        cubicAt(pen, c1, c2, end, float(s) / curveSteps));
                pen = end;
                i += 2;
            }
            break;
        case ClosePoly:
            cur.closed = true;
            pen = subStart;
            break;
        case Stop:
        default:
            break;
        }
        ++i;
    }
    if (!cur.points.empty()) out.push_back(std::move(cur));
    return out;
}

std::vector<Point2D> Path::flatten(int curveSteps) const {
    std::vector<Point2D> out;
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto& sp : toPolylines(curveSteps)) {
        if (!out.empty()) out.push_back({nan, nan});
        out.insert(out.end(), sp.points.begin(), sp.points.end());
        if (sp.closed && !sp.points.empty()) out.push_back(sp.points.front());
    }
    return out;
}

std::pair<Point2D, Point2D> Path::bounds() const {
    if (vertices.empty()) return {{0, 0}, {0, 0}};
    Point2D lo{std::numeric_limits<float>::max(),
               std::numeric_limits<float>::max()};
    Point2D hi{std::numeric_limits<float>::lowest(),
               std::numeric_limits<float>::lowest()};
    for (size_t i = 0; i < vertices.size(); ++i) {
        // CLOSEPOLY carries a dummy vertex — skip it.
        if (i < codes.size() && codes[i] == ClosePoly) continue;
        Point2D p = vertices[i];
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    return {lo, hi};
}

namespace {

bool pointInRing(Point2D p, std::span<const Point2D> ring) {
    bool inside = false;
    size_t n = ring.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Point2D a = ring[i], b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

} // namespace

bool Path::containsPoint(Point2D p, int curveSteps) const {
    bool inside = false;
    for (const auto& sp : toPolylines(curveSteps))
        if (pointInRing(p, sp.points)) inside = !inside;
    return inside;
}

Path Path::transformed(const Transform& t) const {
    Path out = *this;
    for (auto& v : out.vertices) v = t.apply(v);
    return out;
}

// ─── unit shapes ──────────────────────────────────────────────────────────

Path Path::unitCircle() {
    Path p;
    for (int i = 0; i < 64; ++i) {
        float a = kPi * 2.0f * float(i) / 64.0f;
        i == 0 ? p.moveTo({std::cos(a), std::sin(a)})
               : p.lineTo({std::cos(a), std::sin(a)});
    }
    p.close();
    return p;
}

Path Path::unitRectangle() {
    return rectangle(-0.5f, -0.5f, 1.0f, 1.0f);
}

Path Path::unitRegularPolygon(int n) {
    Path p;
    for (int i = 0; i < n; ++i) {
        float a = kPi * 2.0f * float(i) / float(n);
        i == 0 ? p.moveTo({std::cos(a), std::sin(a)})
               : p.lineTo({std::cos(a), std::sin(a)});
    }
    p.close();
    return p;
}

Path Path::unitStar(int n) {
    Path p;
    const float inner = 0.381966f; // mpl golden-ratio inner radius
    for (int i = 0; i < 2 * n; ++i) {
        float r = (i % 2 == 0) ? 1.0f : inner;
        float a = kPi * float(i) / float(n);
        i == 0 ? p.moveTo({r * std::cos(a), r * std::sin(a)})
               : p.lineTo({r * std::cos(a), r * std::sin(a)});
    }
    p.close();
    return p;
}

Path Path::unitAsterisk(int n) {
    Path p;
    for (int i = 0; i < n; ++i) {
        float a = kPi * float(i) / float(n);
        Point2D d{std::cos(a), std::sin(a)};
        p.moveTo({-d.x, -d.y});
        p.lineTo({d.x, d.y});
    }
    return p;
}

Path Path::unitWedge(float t1, float t2, float innerR) {
    Path p;
    int steps = 32;
    for (int i = 0; i <= steps; ++i) {
        float a = (t1 + (t2 - t1) * float(i) / steps) * kPi / 180.0f;
        i == 0 ? p.moveTo({std::cos(a), std::sin(a)})
               : p.lineTo({std::cos(a), std::sin(a)});
    }
    if (innerR > 0.0f) {
        for (int i = steps; i >= 0; --i) {
            float a = (t1 + (t2 - t1) * float(i) / steps) * kPi / 180.0f;
            p.lineTo({innerR * std::cos(a), innerR * std::sin(a)});
        }
    } else {
        p.lineTo({0, 0});
    }
    p.close();
    return p;
}

Path Path::rectangle(float x, float y, float w, float h) {
    Path p;
    p.moveTo({x, y});
    p.lineTo({x + w, y});
    p.lineTo({x + w, y + h});
    p.lineTo({x, y + h});
    p.close();
    return p;
}

Path Path::ellipse(Point2D c, float rx, float ry, float angleDeg) {
    Path p;
    float ca = std::cos(angleDeg * kPi / 180.0f);
    float sa = std::sin(angleDeg * kPi / 180.0f);
    for (int i = 0; i < 64; ++i) {
        float a = kPi * 2.0f * float(i) / 64.0f;
        float ex = rx * std::cos(a), ey = ry * std::sin(a);
        Point2D v{c.x + ex * ca - ey * sa, c.y + ex * sa + ey * ca};
        i == 0 ? p.moveTo(v) : p.lineTo(v);
    }
    p.close();
    return p;
}

// ─── ear clipping ─────────────────────────────────────────────────────────

namespace {

float cross2(Point2D o, Point2D a, Point2D b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

} // namespace

std::vector<Point2D> earClip(std::span<const Point2D> ringIn) {
    // Normalize to a clean open ring.
    std::vector<Point2D> ring(ringIn.begin(), ringIn.end());
    while (ring.size() > 1 &&
           std::abs(ring.front().x - ring.back().x) < 1e-9f &&
           std::abs(ring.front().y - ring.back().y) < 1e-9f)
        ring.pop_back();
    std::vector<Point2D> out;
    size_t n = ring.size();
    if (n < 3) return out;

    // Signed area → winding.
    float area = 0;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        area += ring[j].x * ring[i].y - ring[i].x * ring[j].y;
    if (area < 0) std::reverse(ring.begin(), ring.end()); // want CCW

    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;

    size_t guard = 0;
    while (idx.size() > 3 && guard++ < n * n) {
        bool clipped = false;
        size_t m = idx.size();
        for (size_t i = 0; i < m; ++i) {
            Point2D a = ring[idx[(i + m - 1) % m]];
            Point2D b = ring[idx[i]];
            Point2D c = ring[idx[(i + 1) % m]];
            if (cross2(a, b, c) <= 1e-12f) continue; // reflex/collinear
            bool ear = true;
            for (size_t k = 0; k < m && ear; ++k) {
                if (k == (i + m - 1) % m || k == i || k == (i + 1) % m)
                    continue;
                Point2D p = ring[idx[k]];
                if (cross2(a, b, p) >= -1e-9f &&
                    cross2(b, c, p) >= -1e-9f &&
                    cross2(c, a, p) >= -1e-9f)
                    ear = false;
            }
            if (ear) {
                out.push_back(a); out.push_back(b); out.push_back(c);
                idx.erase(idx.begin() + ptrdiff_t(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) break; // degenerate — emit fan fallback below
    }
    if (idx.size() == 3)
        for (auto k : idx) out.push_back(ring[k]);
    else if (out.empty() && n >= 3) {
        // Fallback: fan triangulation (safe for convex/star shapes).
        for (size_t i = 1; i + 1 < n; ++i) {
            out.push_back(ring[0]);
            out.push_back(ring[i]);
            out.push_back(ring[i + 1]);
        }
    }
    return out;
}

// ─── segment-in-polygon clipping ──────────────────────────────────────────

std::vector<std::pair<Point2D, Point2D>>
clipSegmentToPolygon(Point2D a, Point2D b, std::span<const Point2D> poly) {
    std::vector<float> ts{0.0f, 1.0f};
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Point2D e0 = poly[j], e1 = poly[i];
        // Solve a + t(b-a) = e0 + u(e1-e0).
        float dx = b.x - a.x, dy = b.y - a.y;
        float ex = e1.x - e0.x, ey = e1.y - e0.y;
        float denom = dx * ey - dy * ex;
        if (std::abs(denom) < 1e-12f) continue;
        float t = ((e0.x - a.x) * ey - (e0.y - a.y) * ex) / denom;
        float u = ((e0.x - a.x) * dy - (e0.y - a.y) * dx) / denom;
        if (t > 0.0f && t < 1.0f && u >= 0.0f && u <= 1.0f)
            ts.push_back(t);
    }
    std::ranges::sort(ts);
    std::vector<std::pair<Point2D, Point2D>> out;
    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        float t0 = ts[i], t1 = ts[i + 1];
        if (t1 - t0 < 1e-7f) continue;
        float tm = (t0 + t1) * 0.5f;
        Point2D mid{a.x + (b.x - a.x) * tm, a.y + (b.y - a.y) * tm};
        if (pointInRing(mid, poly)) {
            out.push_back({{a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0},
                           {a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1}});
        }
    }
    return out;
}

} // namespace volcano::plot
