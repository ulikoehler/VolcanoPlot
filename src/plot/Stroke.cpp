// volcano/plot/Stroke.cpp — CPU polyline stroker
#include <volcano/plot/Stroke.hpp>

#include <cmath>
#include <initializer_list>

namespace volcano::plot {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kRoundSegs = 10;   // arc segments for round joins/caps

struct V2 { float x, y; };
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, float s) { return {a.x * s, a.y * s}; }
float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
float cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
float len(V2 a) { return std::sqrt(dot(a, a)); }
V2 norm(V2 a) { float l = len(a); return l > 1e-9f ? a * (1.0f / l) : V2{0, 0}; }
V2 perp(V2 a) { return {-a.y, a.x}; }   // left normal (Y-down pixel space)

void tri(TriMesh& m, V2 a, V2 b, V2 c) {
    m.verts.push_back({a.x, a.y});
    m.verts.push_back({b.x, b.y});
    m.verts.push_back({c.x, c.y});
}

// Emit a segment quad from a to b with half-width h.
void segQuad(TriMesh& m, V2 a, V2 b, V2 n, float h) {
    V2 na = n * h;
    V2 al = a + na, ar = a - na, bl = b + na, br = b - na;
    tri(m, al, bl, br);
    tri(m, al, br, ar);
}

// Fan of triangles around center c from radius-h point a0 to a1
// sweeping through angle [t0, t1].
void fan(TriMesh& m, V2 c, float h, float t0, float t1) {
    int n = std::max(2, int(std::ceil(std::fabs(t1 - t0) / (kPi / kRoundSegs))));
    for (int i = 0; i < n; ++i) {
        float a0 = t0 + (t1 - t0) * float(i) / n;
        float a1 = t0 + (t1 - t0) * float(i + 1) / n;
        tri(m, c,
            {c.x + std::cos(a0) * h, c.y + std::sin(a0) * h},
            {c.x + std::cos(a1) * h, c.y + std::sin(a1) * h});
    }
}

void capEnd(TriMesh& m, V2 end, V2 dir, float h, CapStyle cap) {
    if (cap == CapStyle::Butt) return;
    V2 n = perp(dir);
    if (cap == CapStyle::Projecting) {
        V2 e = end + dir * h;
        V2 na = n * h;
        tri(m, end + na, e + na, e - na);
        tri(m, end + na, e - na, end - na);
        return;
    }
    // Round cap: semicircle around `end`, outside the segment. n is dir
    // rotated +90°, so the outer semicircle spans [t0-π, t0] passing
    // through the outward direction.
    float t0 = std::atan2(n.y, n.x);            // left normal angle
    fan(m, end, h, t0 - kPi, t0);
}

void strokeRun(TriMesh& m, std::span<const Point2D> pts,
               const StrokeParams& p) {
    if (pts.size() < 2) return;
    float h = p.width * 0.5f;
    if (h <= 0.0f) return;

    size_t n = pts.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        V2 a{pts[i].x, pts[i].y}, b{pts[i + 1].x, pts[i + 1].y};
        V2 d = norm(b - a);
        if (len(d) < 1e-9f) continue;
        V2 nrm = perp(d);
        segQuad(m, a, b, nrm, h);

        // Join with the next segment.
        if (i + 2 < n) {
            V2 c{pts[i + 2].x, pts[i + 2].y};
            V2 d2 = norm(c - b);
            if (len(d2) < 1e-9f) continue;
            V2 nrm2 = perp(d2);
            float cr = cross(d, d2);
            if (std::fabs(cr) < 1e-9f) continue;   // collinear
            // Outer side: cr > 0 → turn toward -nrm (right in Y-down space
            // is where perp flips); the outer side is opposite the turn.
            // outer vertex offsets on side s = -sign(cr): that side's
            // offset vertices don't overlap between the two quads.
            float s = cr > 0 ? -1.0f : 1.0f;
            V2 oa = b + nrm * (h * s);    // outer end of seg i
            V2 ob = b + nrm2 * (h * s);   // outer start of seg i+1
            switch (p.join) {
            case JoinStyle::Bevel:
                tri(m, b, oa, ob);
                break;
            case JoinStyle::Round: {
                float a0 = std::atan2(oa.y - b.y, oa.x - b.x);
                float a1 = std::atan2(ob.y - b.y, ob.x - b.x);
                // choose the short sweep direction
                float sweep = a1 - a0;
                while (sweep > kPi) sweep -= 2 * kPi;
                while (sweep < -kPi) sweep += 2 * kPi;
                fan(m, b, h, a0, a0 + sweep);
                break;
            }
            case JoinStyle::Miter:
            default: {
                // miter point along the bisector of the two normals
                V2 bis = norm(nrm + nrm2) * s;
                float cosHalf = std::fabs(dot(bis, nrm));
                float miterLen = cosHalf > 1e-3f ? h / cosHalf : 1e9f;
                if (miterLen <= p.miterLimit * h) {
                    V2 mp = b + bis * miterLen;
                    tri(m, b, oa, mp);
                    tri(m, b, mp, ob);
                } else {
                    tri(m, b, oa, ob);  // fall back to bevel
                }
                break;
            }
            }
        }
    }

    // End caps.
    V2 d0 = norm(V2{pts[1].x, pts[1].y} - V2{pts[0].x, pts[0].y});
    V2 dn = norm(V2{pts[n - 1].x, pts[n - 1].y} -
                 V2{pts[n - 2].x, pts[n - 2].y});
    capEnd(m, {pts[0].x, pts[0].y}, d0 * -1.0f, h, p.cap);
    capEnd(m, {pts[n - 1].x, pts[n - 1].y}, dn, h, p.cap);
}

} // namespace

std::vector<std::vector<Point2D>>
dashSplit(std::span<const Point2D> points, std::span<const float> dashes,
          float dashOffset) {
    std::vector<std::vector<Point2D>> runs;
    if (points.size() < 2 || dashes.empty()) {
        if (points.size() >= 2) runs.emplace_back(points.begin(), points.end());
        return runs;
    }

    // Total pattern length.
    float period = 0.0f;
    for (float d : dashes) period += d;
    if (period <= 0.0f) {
        runs.emplace_back(points.begin(), points.end());
        return runs;
    }

    // Consume the dash offset.
    float off = std::fmod(dashOffset, period);
    if (off < 0) off += period;
    size_t di = 0;
    while (off >= dashes[di] && dashes[di] > 0) { off -= dashes[di]; di = (di + 1) % dashes.size(); }
    float remain = dashes[di] - off;   // remaining length in current phase
    bool on = (di % 2) == 0;

    std::vector<Point2D> cur;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        Point2D a = points[i], b = points[i + 1];
        float segLen = std::hypot(b.x - a.x, b.y - a.y);
        if (segLen <= 1e-9f) continue;
        V2 dir{ (b.x - a.x) / segLen, (b.y - a.y) / segLen };
        float pos = 0.0f;
        while (pos < segLen - 1e-9f) {
            float step = std::min(remain, segLen - pos);
            Point2D at{a.x + dir.x * pos, a.y + dir.y * pos};
            Point2D bt{a.x + dir.x * (pos + step), a.y + dir.y * (pos + step)};
            if (on) {
                if (cur.empty()) cur.push_back(at);
                cur.push_back(bt);
            }
            pos += step;
            remain -= step;
            if (remain <= 1e-6f) {
                di = (di + 1) % dashes.size();
                remain = dashes[di];
                on = (di % 2) == 0;
                if (!on && !cur.empty()) { runs.push_back(std::move(cur)); cur.clear(); }
            }
        }
    }
    if (!cur.empty()) runs.push_back(std::move(cur));
    return runs;
}

TriMesh strokePolyline(std::span<const Point2D> points,
                       const StrokeParams& params) {
    TriMesh m;
    if (points.size() < 2 || params.width <= 0.0f) return m;

    // Split on non-finite points into continuous runs.
    std::vector<Point2D> run;
    auto flush = [&]() {
        if (run.size() >= 2) {
            if (params.dashes.empty()) {
                strokeRun(m, run, params);
            } else {
                for (auto& sub : dashSplit(run, params.dashes,
                                           params.dashOffset))
                    strokeRun(m, sub, params);
            }
        }
        run.clear();
    };
    for (const auto& p : points) {
        if (std::isfinite(p.x) && std::isfinite(p.y)) run.push_back(p);
        else flush();
    }
    flush();
    return m;
}

// ─── sketch wobble ────────────────────────────────────────────────────────

std::vector<Point2D> sketchPolyline(std::span<const Point2D> points,
                                    float scale, float length,
                                    uint64_t seed) {
    if (points.size() < 2 || scale <= 0.0f)
        return {points.begin(), points.end()};
    // Resample at ~length/8 spacing, then offset each interior sample
    // perpendicular to the local direction with a smooth pseudo-random
    // profile (sinusoidal sum decorrelated by a splitmix64 hash).
    auto rng = [s = seed](uint64_t i) mutable {
        s += 0x9e3779b97f4a7c15ULL + i;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return (z ^ (z >> 31)) / 18446744073709551616.0;
    };
    float step = std::max(4.0f, length * 0.125f);
    std::vector<Point2D> out;
    Point2D prev = points[0];
    out.push_back(prev);
    float arc = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        Point2D a = prev, b = points[i];
        float segLen = std::hypot(b.x - a.x, b.y - a.y);
        int n = std::max(1, int(segLen / step));
        for (int s = 1; s <= n; ++s) {
            float t = float(s) / n;
            Point2D p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            arc += segLen / n;
            float ph = rng(i * 131 + s) * 6.2831853f;
            float wob = scale * 1.5f *
                        std::sin(6.2831853f * arc / length + ph);
            float nx = -(b.y - a.y), ny = (b.x - a.x);
            float nl = std::hypot(nx, ny);
            if (nl > 1e-9f) { p.x += nx / nl * wob; p.y += ny / nl * wob; }
            out.push_back(p);
        }
        prev = b;
    }
    return out;
}


MarkerGeom markerGeom(MarkerStyle style, int numsides, float angle) {
    MarkerGeom g;
    auto ngon = [&](int n, float r = 0.5f, float phase = -kPi / 2.0f) {
        std::vector<Point2D> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i) {
            float a = phase + angle + i * (2.0f * kPi / n);
            out.push_back({r * std::cos(a), r * std::sin(a)});
        }
        return out;
    };
    auto cross = [&](std::initializer_list<Point2D> a,
                     std::initializer_list<Point2D> b) {
        g.strokes.push_back(std::vector<Point2D>(a));
        g.strokes.push_back(std::vector<Point2D>(b));
        g.filled = false;
    };
    switch (style) {
    case MarkerStyle::None: break;
    case MarkerStyle::Point:  g.outline = ngon(20, 0.35f); break;
    case MarkerStyle::Circle: g.outline = ngon(20); break;
    case MarkerStyle::Square: g.outline = ngon(4, 0.5f, -kPi / 4.0f); break;
    case MarkerStyle::Diamond: g.outline = ngon(4, 0.6f); break;
    case MarkerStyle::ThinDiamond: {
        float c = std::cos(angle), s = std::sin(angle);
        Point2D pts[4] = {{0,-0.7f},{0.4f,0},{0,0.7f},{-0.4f,0}};
        for (auto p : pts)
            g.outline.push_back({p.x*c - p.y*s, p.x*s + p.y*c});
        break;
    }
    case MarkerStyle::Triangle: g.outline = ngon(3); break;
    case MarkerStyle::TriDown: g.outline = ngon(3, 0.5f, kPi/2.0f); break;
    case MarkerStyle::TriLeft: g.outline = ngon(3, 0.5f, kPi); break;
    case MarkerStyle::TriRight: g.outline = ngon(3, 0.5f, 0.0f); break;
    case MarkerStyle::Tri1:   // Y tripod: 3 spokes (stroke)
        g.strokes = {{{0,0},{0,-0.5f}},
                     {{0,0},{0.433f,0.25f}},
                     {{0,0},{-0.433f,0.25f}}};
        g.filled = false; break;
    case MarkerStyle::Tri2:
        g.strokes = {{{0,0},{0,0.5f}},
                     {{0,0},{0.433f,-0.25f}},
                     {{0,0},{-0.433f,-0.25f}}};
        g.filled = false; break;
    case MarkerStyle::Tri3:
        g.strokes = {{{0,0},{-0.5f,0}},
                     {{0,0},{0.25f,-0.433f}},
                     {{0,0},{0.25f,0.433f}}};
        g.filled = false; break;
    case MarkerStyle::Tri4:
        g.strokes = {{{0,0},{0.5f,0}},
                     {{0,0},{-0.25f,-0.433f}},
                     {{0,0},{-0.25f,0.433f}}};
        g.filled = false; break;
    case MarkerStyle::Plus:
        cross({{-0.5f,0},{0.5f,0}}, {{0,-0.5f},{0,0.5f}}); break;
    case MarkerStyle::X:
        cross({{-0.35f,-0.35f},{0.35f,0.35f}},
              {{-0.35f,0.35f},{0.35f,-0.35f}}); break;
    case MarkerStyle::PlusFilled: {  // thick plus as a 12-gon
        const float a = 0.1667f;  // half arm thickness
        Point2D pts[12] = {
            {-a,-0.5f},{a,-0.5f},{a,-a},{0.5f,-a},{0.5f,a},{a,a},
            {a,0.5f},{-a,0.5f},{-a,a},{-0.5f,a},{-0.5f,-a},{-a,-a}};
        g.outline.assign(pts, pts + 12); break;
    }
    case MarkerStyle::XFilled: {
        const float a = 0.1667f;
        Point2D pts[12] = {
            {-a,-0.5f},{a,-0.5f},{a,-a},{0.5f,-a},{0.5f,a},{a,a},
            {a,0.5f},{-a,0.5f},{-a,a},{-0.5f,a},{-0.5f,-a},{-a,-a}};
        float c = std::cos(kPi/4.0f), s = std::sin(kPi/4.0f);
        for (auto p : pts)
            g.outline.push_back({(p.x*c - p.y*s)*1.1f, (p.x*s + p.y*c)*1.1f});
        break;
    }
    case MarkerStyle::Star: {  // 5-point star
        for (int i = 0; i < 10; ++i) {
            float r = (i % 2 == 0) ? 0.5f : 0.21f;
            float a = -kPi / 2.0f + i * kPi / 5.0f;
            g.outline.push_back({r * std::cos(a), r * std::sin(a)});
        }
        break;
    }
    case MarkerStyle::Pentagon: g.outline = ngon(5); break;
    case MarkerStyle::Hexagon1: g.outline = ngon(6); break;
    case MarkerStyle::Hexagon2: g.outline = ngon(6, 0.5f, 0.0f); break;
    case MarkerStyle::Octagon: g.outline = ngon(8, 0.5f, -kPi/8.0f); break;
    case MarkerStyle::VLine:
        g.strokes = {{{0,-0.5f},{0,0.5f}}}; g.filled = false; break;
    case MarkerStyle::HLine:
        g.strokes = {{{-0.5f,0},{0.5f,0}}}; g.filled = false; break;
    case MarkerStyle::TickLeft:
        g.strokes = {{{0,0},{-0.5f,0}}}; g.filled = false; break;
    case MarkerStyle::TickRight:
        g.strokes = {{{0,0},{0.5f,0}}}; g.filled = false; break;
    case MarkerStyle::TickUp:
        g.strokes = {{{0,0},{0,-0.5f}}}; g.filled = false; break;
    case MarkerStyle::TickDown:
        g.strokes = {{{0,0},{0,0.5f}}}; g.filled = false; break;
    case MarkerStyle::CaretLeft:
        g.strokes = {{{-0.3f,0},{0.3f,-0.4f},{0.3f,0.4f}}};
        g.filled = false; break;
    case MarkerStyle::CaretRight:
        g.strokes = {{{0.3f,0},{-0.3f,-0.4f},{-0.3f,0.4f}}};
        g.filled = false; break;
    case MarkerStyle::CaretUp:
        g.strokes = {{{0,-0.3f},{-0.4f,0.3f},{0.4f,0.3f}}};
        g.filled = false; break;
    case MarkerStyle::CaretDown:
        g.strokes = {{{0,0.3f},{-0.4f,-0.3f},{0.4f,-0.3f}}};
        g.filled = false; break;
    case MarkerStyle::CaretLeftBase:
        g.strokes = {{{-0.35f,0.3f},{0.35f,-0.4f},{0.35f,0.4f}}};
        g.filled = false; break;
    case MarkerStyle::CaretRightBase:
        g.strokes = {{{0.35f,0.3f},{-0.35f,-0.4f},{-0.35f,0.4f}}};
        g.filled = false; break;
    case MarkerStyle::CaretUpBase:
        g.strokes = {{{-0.35f,-0.3f},{-0.4f,0.35f},{0.4f,0.35f}}};
        g.filled = false; break;
    case MarkerStyle::CaretDownBase:
        g.strokes = {{{-0.35f,-0.35f},{-0.4f,-0.3f},{0.4f,-0.3f}}};
        g.filled = false; break;
    case MarkerStyle::Polygon: g.outline = ngon(numsides); break;
    case MarkerStyle::StarN: {
        for (int i = 0; i < numsides * 2; ++i) {
            float r = (i % 2 == 0) ? 0.5f : 0.21f;
            float a = -kPi / 2.0f + i * kPi / numsides;
            g.outline.push_back({r * std::cos(a), r * std::sin(a)});
        }
        break;
    }
    case MarkerStyle::AsteriskN: {
        for (int i = 0; i < numsides; ++i) {
            float a = -kPi / 2.0f + i * 2.0f * kPi / numsides;
            g.strokes.push_back({{0,0},
                {0.5f * std::cos(a), 0.5f * std::sin(a)}});
        }
        g.filled = false; break;
    }
    case MarkerStyle::CircledN: g.outline = ngon(numsides); break;
    }
    // Apply rotation to strokes as well.
    if (angle != 0.0f && !g.filled) {
        float c = std::cos(angle), s = std::sin(angle);
        for (auto& st : g.strokes)
            for (auto& p : st)
                p = {p.x * c - p.y * s, p.x * s + p.y * c};
    }
    return g;
}

} // namespace volcano::plot
