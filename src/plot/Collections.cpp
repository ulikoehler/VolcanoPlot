// volcano/plot/Collections.cpp — patch primitives + collection rendering
#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/render/primitives/InstancedPathRenderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace volcano::plot {

// ═══ patch primitives ═════════════════════════════════════════════════════

// ─── mpl BoxStyle ─────────────────────────────────────────────────────────

namespace {

std::string trimStr(std::string s) {
    auto ws = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && ws(s.front())) s.erase(s.begin());
    while (!s.empty() && ws(s.back())) s.pop_back();
    return s;
}

// mpl Sawtooth._get_sawtooth_vertices: interleaved tooth/edge vertices.
std::vector<Point2D> sawtoothVertices(float x0, float y0, float w, float h,
                                      const BoxStyleSpec& s) {
    float pad = s.mutationSize * s.pad;
    float ts = s.toothSize >= 0.0f ? s.toothSize * s.mutationSize
                                   : s.pad * 0.5f * s.mutationSize;
    float hsz = ts / 2;
    float width = w + 2 * pad - ts;
    float height = h + 2 * pad - ts;
    if (ts <= 1e-9f || width <= ts || height <= ts)
        return {{x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}};

    int dsx_n = int(std::lround((width - ts) / (ts * 2))) * 2;
    int dsy_n = int(std::lround((height - ts) / (ts * 2))) * 2;

    float ax0 = x0 - pad + hsz, ay0 = y0 - pad + hsz;
    float ax1 = ax0 + width, ay1 = ay0 + height;

    auto linspace = [](float a, float b, int n) {
        std::vector<float> out;
        for (int i = 0; i < n; ++i)
            out.push_back(a + (b - a) * (n > 1 ? float(i) / (n - 1) : 0.0f));
        return out;
    };
    // xs pattern per mpl:
    //  bottom: x0, linspace(x0+hsz, x1-hsz, 2dsx_n+1)
    //  right:  [x1, x1+hsz, x1, x1-hsz]*dsy_n, first 2dsy_n+2
    //  top:    x1, linspace(x1-hsz, x0+hsz, 2dsx_n+1)
    //  left:   [x0, x0-hsz, x0, x0+hsz]*dsy_n, first 2dsy_n+2
    auto toothRun = [](float a, float b, float c, float d, int n, int take) {
        std::vector<float> out;
        float pat[4] = {a, b, c, d};
        for (int i = 0; i < take; ++i) out.push_back(pat[i % 4]);
        (void)n;
        return out;
    };
    std::vector<float> xs, ys;
    xs.push_back(ax0);
    for (float v : linspace(ax0 + hsz, ax1 - hsz, 2 * dsx_n + 1)) xs.push_back(v);
    for (float v : toothRun(ax1, ax1 + hsz, ax1, ax1 - hsz, dsy_n, 2 * dsy_n + 2))
        xs.push_back(v);
    xs.push_back(ax1);
    for (float v : linspace(ax1 - hsz, ax0 + hsz, 2 * dsx_n + 1)) xs.push_back(v);
    for (float v : toothRun(ax0, ax0 - hsz, ax0, ax0 + hsz, dsy_n, 2 * dsy_n + 2))
        xs.push_back(v);

    for (float v : toothRun(ay0, ay0 - hsz, ay0, ay0 + hsz, dsx_n, 2 * dsx_n + 2))
        ys.push_back(v);
    ys.push_back(ay0);
    for (float v : linspace(ay0 + hsz, ay1 - hsz, 2 * dsy_n + 1)) ys.push_back(v);
    for (float v : toothRun(ay1, ay1 + hsz, ay1, ay1 - hsz, dsx_n, 2 * dsx_n + 2))
        ys.push_back(v);
    ys.push_back(ay1);
    for (float v : linspace(ay1 - hsz, ay0 + hsz, 2 * dsy_n + 1)) ys.push_back(v);

    std::vector<Point2D> out;
    for (size_t i = 0; i < xs.size(); ++i) out.push_back({xs[i], ys[i]});
    return out;
}

} // namespace

std::optional<BoxStyleSpec> parseBoxStyle(std::string_view sv) {
    std::string s{sv};
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true) {
        size_t c = s.find(',', pos);
        parts.push_back(trimStr(s.substr(pos, c == std::string::npos
                                              ? c : c - pos)));
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    if (parts.empty() || parts[0].empty()) return std::nullopt;
    std::string name = parts[0];
    std::ranges::transform(name, name.begin(),
                           [](char c) { return char(std::tolower(c)); });
    BoxStyleSpec spec;
    using K = BoxStyleSpec::Kind;
    static const std::pair<const char*, K> kinds[] = {
        {"square", K::Square}, {"circle", K::Circle}, {"ellipse", K::Ellipse},
        {"round", K::Round}, {"round4", K::Round4},
        {"sawtooth", K::Sawtooth}, {"roundtooth", K::Roundtooth},
        {"larrow", K::LArrow}, {"rarrow", K::RArrow}, {"darrow", K::DArrow}};
    bool found = false;
    for (auto& [n, k] : kinds)
        if (name == n) { spec.kind = k; found = true; break; }
    if (!found) return std::nullopt;
    for (size_t i = 1; i < parts.size(); ++i) {
        auto eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        std::string key = trimStr(parts[i].substr(0, eq));
        std::string val = trimStr(parts[i].substr(eq + 1));
        float f = 0.0f;
        try { f = std::stof(val); } catch (...) { continue; }
        if (key == "pad") spec.pad = f;
        else if (key == "rounding_size") spec.roundingSize = f;
        else if (key == "tooth_size") spec.toothSize = f;
        else if (key == "mutation_scale") spec.mutationSize = f;
    }
    return spec;
}

Path boxStylePath(float x0, float y0, float w, float h,
                  const BoxStyleSpec& spec) {
    using K = BoxStyleSpec::Kind;
    float ms = spec.mutationSize;
    float pad = ms * spec.pad;
    float x1 = x0 + w, y1 = y0 + h;
    Path p;
    switch (spec.kind) {
    case K::Square: {
        // mpl: pad the rect on all sides.
        float ax0 = x0 - pad, ay0 = y0 - pad;
        float ax1 = x1 + pad, ay1 = y1 + pad;
        p.moveTo({ax0, ay0});
        p.lineTo({ax1, ay0}); p.lineTo({ax1, ay1}); p.lineTo({ax0, ay1});
        p.close();
        return p;
    }
    case K::Circle: {
        float width = w + 2 * pad, height = h + 2 * pad;
        float ax0 = x0 - pad, ay0 = y0 - pad;
        float r = std::max(width, height) / 2;
        return Path::ellipse({ax0 + width / 2, ay0 + height / 2}, r, r);
    }
    case K::Ellipse: {
        float width = w + 2 * pad, height = h + 2 * pad;
        float ax0 = x0 - pad, ay0 = y0 - pad;
        return Path::ellipse({ax0 + width / 2, ay0 + height / 2},
                             width / std::sqrt(2.0f), height / std::sqrt(2.0f));
    }
    case K::LArrow:
    case K::RArrow:
    case K::DArrow: {
        float height = h + 2 * pad;
        // LArrow/RArrow pad width by 2*pad; DArrow pads x via the arrows.
        float width = spec.kind == K::DArrow ? w : w + 2 * pad;
        float ax0 = x0 - pad, ay0 = y0 - pad;
        float ax1 = spec.kind == K::DArrow ? x1 : ax0 + width;
        float ay1 = ay0 + height;
        float dx = (ay1 - ay0) / 2;
        float dxx = dx / 2;
        ax0 += pad / 1.4f;  // adjust by ~sqrt(2)
        if (spec.kind == K::DArrow) {
            p.moveTo({ax0 + dxx, ay0});
            p.lineTo({ax1, ay0});
            p.lineTo({ax1, ay0 - dxx}); p.lineTo({ax1 + dx + dxx, ay0 + dx});
            p.lineTo({ax1, ay1 + dxx});
            p.lineTo({ax1, ay1});
            p.lineTo({ax0 + dxx, ay1});
            p.lineTo({ax0 + dxx, ay1 + dxx}); p.lineTo({ax0 - dx, ay0 + dx});
            p.lineTo({ax0 + dxx, ay0 - dxx});
            p.close();
        } else {
            // LArrow; RArrow mirrors x about the rect centre.
            auto mk = [&](bool mirror) {
                Path q;
                auto pt = [&](float px, float py) {
                    return mirror ? Point2D{2 * x0 + w - px, py}
                                  : Point2D{px, py};
                };
                q.moveTo(pt(ax0 + dxx, ay0));
                q.lineTo(pt(ax1, ay0)); q.lineTo(pt(ax1, ay1));
                q.lineTo(pt(ax0 + dxx, ay1));
                q.lineTo(pt(ax0 + dxx, ay1 + dxx));
                q.lineTo(pt(ax0 - dx, ay0 + dx));
                q.lineTo(pt(ax0 + dxx, ay0 - dxx));
                q.close();
                return q;
            };
            return mk(spec.kind == K::RArrow);
        }
        return p;
    }
    case K::Round: {
        float dr = spec.roundingSize >= 0.0f ? ms * spec.roundingSize : pad;
        float ax0 = x0 - pad, ay0 = y0 - pad;
        float ax1 = x1 + pad, ay1 = y1 + pad;
        // mpl: quadratic-bezier corners.
        p.moveTo({ax0 + dr, ay0});
        p.lineTo({ax1 - dr, ay0});
        p.curve3({ax1, ay0}, {ax1, ay0 + dr});
        p.lineTo({ax1, ay1 - dr});
        p.curve3({ax1, ay1}, {ax1 - dr, ay1});
        p.lineTo({ax0 + dr, ay1});
        p.curve3({ax0, ay1}, {ax0, ay1 - dr});
        p.lineTo({ax0, ay0 + dr});
        p.curve3({ax0, ay0}, {ax0 + dr, ay0});
        p.close();
        return p;
    }
    case K::Round4: {
        float dr = spec.roundingSize >= 0.0f ? ms * spec.roundingSize
                                             : pad / 2.0f;
        float ax0 = x0 - pad + dr, ay0 = y0 - pad + dr;
        float ax1 = x1 + pad - dr, ay1 = y1 + pad - dr;
        p.moveTo({ax0, ay0});
        p.curve4({ax0 + dr, ay0 - dr}, {ax1 - dr, ay0 - dr}, {ax1, ay0});
        p.curve4({ax1 + dr, ay0 + dr}, {ax1 + dr, ay1 - dr}, {ax1, ay1});
        p.curve4({ax1 - dr, ay1 + dr}, {ax0 + dr, ay1 + dr}, {ax0, ay1});
        p.curve4({ax0 - dr, ay1 - dr}, {ax0 - dr, ay0 + dr}, {ax0, ay0});
        p.close();
        return p;
    }
    case K::Sawtooth: {
        auto vs = sawtoothVertices(x0, y0, w, h, spec);
        if (!vs.empty()) {
            p.moveTo(vs[0]);
            for (size_t i = 1; i < vs.size(); ++i) p.lineTo(vs[i]);
            p.close();
        }
        return p;
    }
    case K::Roundtooth: {
        auto vs = sawtoothVertices(x0, y0, w, h, spec);
        if (vs.empty()) return p;
        vs.push_back(vs.front());  // mpl appends the first vertex
        p.moveTo(vs[0]);
        for (size_t i = 1; i + 1 < vs.size(); i += 2)
            p.curve3(vs[i], vs[i + 1]);
        p.close();
        return p;
    }
    }
    return p;
}

namespace patch {

Patch Rectangle(float x, float y, float w, float h) {
    return {Path::rectangle(x, y, w, h), {}};
}

Patch Circle(Point2D c, float r) {
    return {Path::ellipse(c, r, r), {}};
}

Patch Ellipse(Point2D c, float w, float h, float angleDeg) {
    return {Path::ellipse(c, w * 0.5f, h * 0.5f, angleDeg), {}};
}

Patch Polygon(std::vector<Point2D> pts, bool closed) {
    Path p;
    for (size_t i = 0; i < pts.size(); ++i)
        i == 0 ? p.moveTo(pts[i]) : p.lineTo(pts[i]);
    if (closed) p.close();
    return {p, {}};
}

Patch Wedge(Point2D c, float r, float t1, float t2) {
    auto p = Path::unitWedge(t1, t2);
    Patch out{Path{}, {}};
    // scale+translate the unit wedge into data coords
    for (auto& v : p.vertices) v = {c.x + v.x * r, c.y + v.y * r};
    out.path = std::move(p);
    return out;
}

Patch FancyBboxPatch(float x, float y, float w, float h, float pad) {
    // Rounded rect: 4 straight sides + 4 quarter-circle corners (Curve4
    // approximation with kappa = 0.5523).
    const float k = 0.5523f;
    float r = std::min({pad, w * 0.5f, h * 0.5f});
    Path p;
    p.moveTo({x + r, y});
    p.lineTo({x + w - r, y});
    p.curve4({x + w - r + k * r, y}, {x + w, y + r - k * r}, {x + w, y + r});
    p.lineTo({x + w, y + h - r});
    p.curve4({x + w, y + h - r + k * r}, {x + w - r + k * r, y + h},
             {x + w - r, y + h});
    p.lineTo({x + r, y + h});
    p.curve4({x + r - k * r, y + h}, {x, y + h - r + k * r},
             {x, y + h - r});
    p.lineTo({x, y + r});
    p.curve4({x, y + r - k * r}, {x + r - k * r, y}, {x + r, y});
    p.close();
    return {p, {}};
}

Patch FancyBboxPatch(float x, float y, float w, float h,
                     const BoxStyleSpec& spec) {
    return {boxStylePath(x, y, w, h, spec), {}};
}

Patch FancyArrowPatch(Point2D a, Point2D b, float headW, float headL) {
    Point2D d{b.x - a.x, b.y - a.y};
    float len = std::hypot(d.x, d.y);
    if (len < 1e-9f) return {Path{}, {}};
    d = {d.x / len, d.y / len};
    Point2D n{-d.y, d.x};
    float hl = std::min(headL, len);
    Point2D neck{b.x - d.x * hl, b.y - d.y * hl};
    Path p;
    p.moveTo(a);
    p.lineTo(neck);
    p.lineTo({neck.x + n.x * headW * 0.5f, neck.y + n.y * headW * 0.5f});
    p.lineTo(b);
    p.lineTo({neck.x - n.x * headW * 0.5f, neck.y - n.y * headW * 0.5f});
    p.lineTo(neck);
    p.close();
    return {p, {}};
}

Patch PathPatch(Path path) { return {std::move(path), {}}; }

} // namespace patch

// ═══ Collection helpers ═══════════════════════════════════════════════════

Point2D Collection::toPx(const Axes& axes, Rect2D rect, Point2D p) {
    Point2D f = axes.dataToFraction(p);
    return {rect.x + f.x * float(rect.width),
            rect.y + (1.0f - f.y) * float(rect.height)};
}

Point2D Collection::offsetPx(const Axes& axes, Rect2D rect,
                             const Transform* offT, Point2D off,
                             Point2D itemOriginPx) {
    if (offT) return offT->apply(off);
    return toPx(axes, rect, off);
}

namespace {

/// Stroke one pixel-space polyline (closed or open) into triangles.
void appendStroke(std::vector<Point2D>& out, std::span<const Point2D> pts,
                  bool closed, float lw, std::span<const float> dashes,
                  float dashOffset) {
    std::vector<Point2D> pp(pts.begin(), pts.end());
    if (closed && pp.size() > 2 &&
        (std::abs(pp.front().x - pp.back().x) > 1e-6f ||
         std::abs(pp.front().y - pp.back().y) > 1e-6f))
        pp.push_back(pp.front());
    if (pp.size() < 2) return;
    StrokeParams sp;
    sp.width = lw;
    sp.dashes.assign(dashes.begin(), dashes.end());
    sp.dashOffset = dashOffset;
    auto mesh = strokePolyline(pp, sp);
    out.insert(out.end(), mesh.verts.begin(), mesh.verts.end());
}

} // namespace

std::vector<Point2D> Collection::clipRingPx(const Axes& axes,
                                            Rect2D rect) const {
    if (!clipPath) return {};
    auto subs = clipPath->toPolylines();
    const Path::Subpath* best = nullptr;
    for (auto& sp : subs)
        if (!best || sp.points.size() > best->points.size()) best = &sp;
    if (!best || best->points.size() < 3) return {};
    std::vector<Point2D> ring;
    ring.reserve(best->points.size());
    for (auto p : best->points) ring.push_back(toPx(axes, rect, p));
    return ring;
}

void Collection::drawSubpaths(vk::CommandBuffer cmd, render::Renderer& r,
                              vk::Rect2D clip, vk::Extent2D res,
                              const std::vector<Path::Subpath>& subs,
                              Color face, Color edge, float lw,
                              std::span<const float> dash,
                              const std::string& hatchStr,
                              float hatchSpacing, float sketchScale,
                              float sketchLength,
                              std::span<const Point2D> clipRing) {
    auto& spine = r.spineRenderer();
    // Fill (ear-clipped per closed subpath).
    if (face.a > 0.0f) {
        std::vector<Point2D> tris;
        for (const auto& sp : subs) {
            if (!sp.closed || sp.points.size() < 3) continue;
            auto t = earClip(sp.points);
            tris.insert(tris.end(), t.begin(), t.end());
        }
        if (!clipRing.empty())
            tris = clipTrianglesToPolygon(tris, clipRing);
        if (!tris.empty())
            spine.drawTriangles(cmd, clip, res, tris, face);
    }
    // Hatch lines clipped to the filled region.
    if (!hatchStr.empty() && face.a > 0.0f) {
        std::vector<Point2D> ht;
        for (const auto& sp : subs) {
            if (!sp.closed || sp.points.size() < 3) continue;
            auto h = hatchTriangles(sp.points, hatchStr, hatchSpacing);
            ht.insert(ht.end(), h.begin(), h.end());
        }
        if (!clipRing.empty())
            ht = clipTrianglesToPolygon(ht, clipRing);
        if (!ht.empty())
            spine.drawTriangles(cmd, clip, res, ht, edge.a > 0 ? edge
                                                             : Color::black());
    }
    // Edge stroke.
    if (edge.a > 0.0f && lw > 0.0f) {
        std::vector<Point2D> ev;
        for (const auto& sp : subs) {
            auto pts = sp.points;
            if (sketchScale > 0.0f)
                pts = sketchPolyline(pts, sketchScale * 2.0f,
                                     sketchLength);
            appendStroke(ev, pts, sp.closed, lw, dash, 0.0f);
        }
        if (!clipRing.empty())
            ev = clipTrianglesToPolygon(ev, clipRing);
        if (!ev.empty()) spine.drawTriangles(cmd, clip, res, ev, edge);
    }
}

// ═══ hatch patterns ═══════════════════════════════════════════════════════

std::vector<Point2D>
hatchTriangles(std::span<const Point2D> poly, std::string_view pattern,
               float spacing) {
    if (poly.size() < 3 || pattern.empty()) return {};
    // Density: each repeated char halves spacing (mpl semantics).
    std::map<char, int> counts;
    for (char c : pattern) counts[c]++;
    Point2D lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    for (auto p : poly) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    float diag = std::hypot(hi.x - lo.x, hi.y - lo.y);
    if (diag < 1e-6f) return {};

    std::vector<Point2D> out;
    auto hatchLines = [&](float dirX, float dirY, float step, float lw) {
        // Perpendicular family: line dir d, sweep along normal n.
        Point2D n{-dirY, dirX};
        Point2D c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
        int half = int(diag / step) + 2;
        for (int i = -half; i <= half; ++i) {
            Point2D base{c.x + n.x * step * i, c.y + n.y * step * i};
            Point2D a{base.x - dirX * diag, base.y - dirY * diag};
            Point2D b{base.x + dirX * diag, base.y + dirY * diag};
            for (auto [s, e] : clipSegmentToPolygon(a, b, poly)) {
                auto mesh = strokePolyline(std::span<const Point2D>{
                                               std::array<Point2D, 2>{s, e}},
                                           StrokeParams{.width = lw});
                out.insert(out.end(), mesh.verts.begin(), mesh.verts.end());
            }
        }
    };

    constexpr float inv2 = 0.70710678f;
    auto emit = [&](char ch, int repeat) {
        float step = spacing / float(repeat);
        float lw = std::max(0.6f, spacing * 0.1f);
        switch (ch) {
        case '/': hatchLines(inv2, -inv2, step, lw); break;
        case '\\': hatchLines(inv2, inv2, step, lw); break;
        case '|': hatchLines(0, 1, step, lw); break;
        case '-': hatchLines(1, 0, step, lw); break;
        case '+': hatchLines(1, 0, step, lw); hatchLines(0, 1, step, lw); break;
        case 'x': hatchLines(inv2, -inv2, step, lw);
                  hatchLines(inv2, inv2, step, lw); break;
        default: break; // 'o', '.', '*' unsupported — silently skip
        }
    };
    for (auto [ch, rep] : counts) emit(ch, rep);
    return out;
}

// ═══ per-collection draw ══════════════════════════════════════════════════

namespace {

template <class T>
const T& at(const std::vector<T>& v, size_t i, const T& fallback) {
    return v.empty() ? fallback : v[i % v.size()];
}

vk::Rect2D clipOf(Rect2D r) {
    return {vk::Offset2D{r.x, r.y}, vk::Extent2D{r.width, r.height}};
}

} // namespace

void PatchCollection::draw(vk::CommandBuffer cmd, render::Renderer& r,
                           const Axes& axes, Rect2D rect) {
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    auto ring = clipRingPx(axes, rect);
    for (const auto& p : patches) {
        Path path = p.style.transform ? p.path.transformed(*p.style.transform)
                                      : p.path;
        auto subs = path.toPolylines();
        for (auto& sp : subs)
            for (auto& pt : sp.points) pt = toPx(axes, rect, pt);
        std::vector<float> dash = p.style.dashes;
        if (dash.empty())
            dash = dashPattern(p.style.lineStyle, p.style.lineWidth);
        drawSubpathsFx(cmd, r, clip, res, subs, p.style.face, p.style.edge,
                       p.style.lineWidth, dash, p.style.hatch,
                       p.style.hatchSpacing, axes.style().sketchScale,
                       axes.style().sketchLength, ring,
                       pathEffects, axes.style().dpi);
    }
}

void PatchCollection::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : patches)
        for (auto pt : p.path.vertices) {
            v.x.min = std::min(v.x.min, pt.x); v.x.max = std::max(v.x.max, pt.x);
            v.y.min = std::min(v.y.min, pt.y); v.y.max = std::max(v.y.max, pt.y);
        }
}

void PathCollection::draw(vk::CommandBuffer cmd, render::Renderer& r,
                          const Axes& axes, Rect2D rect) {
    if (offsets.empty()) return;
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    auto ring = clipRingPx(axes, rect);
    auto proto = path.toPolylines();
    Color defFace = faceColors.empty() ? Color{0.121f, 0.466f, 0.705f, 1}
                                       : Color{};
    Color defEdge = edgeColors.empty() ? Color{0, 0, 0, 0} : Color{};
    // Pixels per data unit (sizes are given in data units; correct for
    // linear scales, approximate otherwise).
    float ppu = float(rect.width) /
                std::max(1e-9f, axes.viewport().x.span());
    float ppv = float(rect.height) /
                std::max(1e-9f, axes.viewport().y.span());

    // GPU fast path: instance the template triangles. Eligible when the
    // per-item work is a pure scale+translate fill — no per-item
    // transforms, hatch, dashes, sketch, clip path, or edge stroke.
    auto& inst = r.instancedPathRenderer();
    bool edgesVisible = std::ranges::any_of(edgeColors,
                                            [](Color c) { return c.a > 0; });
    if (inst.inited() && transforms.empty() && hatch.empty() && ring.empty()
        && !strokeOnly && !edgesVisible
        && axes.style().sketchScale == 0.0f
        && std::ranges::all_of(proto, [](const Path::Subpath& sp) {
               return sp.closed && sp.points.size() >= 3; })) {
        if (templateDirty_) {
            std::vector<Point2D> tris;
            for (const auto& sp : proto) {
                auto t = earClip(sp.points);
                tris.insert(tris.end(), t.begin(), t.end());
            }
            auto& ctx = r.backend().context();
            inst.setTemplate(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), tris);
            templateDirty_ = false;
        }
        std::vector<render::primitives::PathInstance> insts;
        insts.reserve(offsets.size());
        for (size_t i = 0; i < offsets.size(); ++i) {
            Point2D size = at(sizes, i, Point2D{1, 1});
            Point2D center = offsetPx(axes, rect, offsetTransform.get(),
                                      offsets[i], {});
            Color face = at(faceColors, i, defFace);
            insts.push_back({center.x, center.y,
                             size.x * ppu, -size.y * ppv,
                             face.r, face.g, face.b, face.a});
        }
        inst.drawInstanced(cmd, clip, res, insts);
        return;
    }

    for (size_t i = 0; i < offsets.size(); ++i) {
        Point2D size = at(sizes, i, Point2D{1, 1});
        Point2D center = offsetPx(axes, rect, offsetTransform.get(),
                                  offsets[i], {});
        auto subs = proto;
        for (auto& sp : subs)
            for (auto& pt : sp.points) {
                Point2D scaled{pt.x * size.x, pt.y * size.y};
                if (i < transforms.size() && transforms[i])
                    scaled = transforms[i]->apply(scaled);
                pt = {center.x + scaled.x * ppu, center.y - scaled.y * ppv};
            }
        float lw = at(lineWidths, i, 1.0f);
        std::vector<float> dash =
            dashes.empty() ? dashPattern(lineStyle, lw) : dashes;
        Color face = at(faceColors, i, defFace);
        if (strokeOnly) face.a = 0.0f;
        drawSubpathsFx(cmd, r, clip, res, subs, face,
                       at(edgeColors, i, defEdge), lw, dash, hatch,
                       hatchSpacing, axes.style().sketchScale,
                       axes.style().sketchLength, ring,
                       pathEffects, axes.style().dpi);
    }
}

void PathCollection::contributeToAutoscale(Viewport& v) const {
    for (auto o : offsets) {
        v.x.min = std::min(v.x.min, o.x); v.x.max = std::max(v.x.max, o.x);
        v.y.min = std::min(v.y.min, o.y); v.y.max = std::max(v.y.max, o.y);
    }
}

void LineCollection::draw(vk::CommandBuffer cmd, render::Renderer& r,
                          const Axes& axes, Rect2D rect) {
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    auto& spine = r.spineRenderer();
    auto ring = clipRingPx(axes, rect);
    Color def{0.121f, 0.466f, 0.705f, 1};
    const float dpi = axes.style().dpi;
    for (size_t i = 0; i < segments.size(); ++i) {
        std::vector<Point2D> px;
        px.reserve(segments[i].size());
        for (auto p : segments[i]) px.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.5f);
        std::vector<float> dash = dashes.empty() ? dashPattern(lineStyle, lw)
                                                 : dashes;
        Color c = at(edgeColors, i, def);
        // mpl patheffects: one pass per effect (offset/color/width
        // overrides), plus the normal pass after `withX` effects.
        auto pass = [&](Point2D off, Color pc, float plw) {
            std::vector<Point2D> pxs = px;
            for (auto& q : pxs) { q.x += off.x; q.y += off.y; }
            std::vector<std::vector<Point2D>> pieces{std::move(pxs)};
            if (!ring.empty())
                pieces = clipPolylineToPolygon(pieces[0], ring);
            for (auto& piece : pieces) {
                std::vector<Point2D> ev;
                appendStroke(ev, piece, false, plw, dash, 0.0f);
                if (!ev.empty() && pc.a > 0)
                    spine.drawTriangles(cmd, clip, res, ev, pc);
            }
        };
        if (pathEffects.empty()) {
            pass({0.0f, 0.0f}, c, lw);
            continue;
        }
        for (const auto& e : pathEffects) {
            Color pc = c; float plw = lw;
            switch (e.kind) {
            case PathEffect::Kind::Stroke:
                pc = e.foreground.value_or(c);
                plw = e.strokeWidthPx(lw, dpi);
                break;
            case PathEffect::Kind::LineShadow:
            case PathEffect::Kind::PatchShadow:
                pc = e.shadowColor.value_or(
                    Color{c.r * e.rho, c.g * e.rho, c.b * e.rho, 1.0f});
                pc.a = e.shadowAlpha;
                plw = e.strokeWidthPx(lw, dpi);
                break;
            case PathEffect::Kind::Normal: break;
            }
            pass(e.offsetPx(dpi), pc, plw);
            if (e.thenNormal) pass({0.0f, 0.0f}, c, lw);
        }
    }
}

void LineCollection::contributeToAutoscale(Viewport& v) const {
    for (const auto& seg : segments)
        for (auto p : seg) {
            v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
            v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
        }
}

void PolyCollection::draw(vk::CommandBuffer cmd, render::Renderer& r,
                          const Axes& axes, Rect2D rect) {
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    Color defFace{0.121f, 0.466f, 0.705f, 1};
    Color defEdge{0, 0, 0, 0};
    auto ring = clipRingPx(axes, rect);
    for (size_t i = 0; i < polys.size(); ++i) {
        Path::Subpath sp;
        sp.closed = true;
        sp.points.reserve(polys[i].size());
        for (auto p : polys[i]) sp.points.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.0f);
        std::vector<float> dash = dashes.empty() ? dashPattern(lineStyle, lw)
                                                 : dashes;
        drawSubpathsFx(cmd, r, clip, res, {sp},
                       at(faceColors, i, defFace), at(edgeColors, i, defEdge),
                       lw, dash, hatch, hatchSpacing,
                       axes.style().sketchScale, axes.style().sketchLength,
                       ring, pathEffects, axes.style().dpi);
    }
}

void PolyCollection::contributeToAutoscale(Viewport& v) const {
    for (const auto& poly : polys)
        for (auto p : poly) {
            v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
            v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
        }
}

void QuadMesh::draw(vk::CommandBuffer cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (rows == 0 || cols == 0 ||
        corners.size() < size_t(rows + 1) * (cols + 1))
        return;
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    auto& spine = r.spineRenderer();
    auto ring = clipRingPx(axes, rect);
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t col = 0; col < cols; ++col) {
            size_t i = size_t(row) * cols + col;
            Point2D c00 = corners[size_t(row) * (cols + 1) + col];
            Point2D c01 = corners[size_t(row) * (cols + 1) + col + 1];
            Point2D c11 = corners[size_t(row + 1) * (cols + 1) + col + 1];
            Point2D c10 = corners[size_t(row + 1) * (cols + 1) + col];
            std::array<Point2D, 4> q{toPx(axes, rect, c00),
                                     toPx(axes, rect, c01),
                                     toPx(axes, rect, c11),
                                     toPx(axes, rect, c10)};
            std::vector<Point2D> tris{q[0], q[1], q[2], q[0], q[2], q[3]};
            if (!ring.empty())
                tris = clipTrianglesToPolygon(tris, ring);
            if (!tris.empty())
                spine.drawTriangles(cmd, clip, res, tris,
                                    at(cellColors, i, def));
        }
}

void QuadMesh::contributeToAutoscale(Viewport& v) const {
    for (auto p : corners) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}

void TriMeshCollection::draw(vk::CommandBuffer cmd, render::Renderer& r,
                             const Axes& axes, Rect2D rect) {
    vk::Extent2D res = r.backend().extent();
    auto clip = clipOf(rect);
    auto& spine = r.spineRenderer();
    auto ring = clipRingPx(axes, rect);
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (size_t i = 0; i < triangles.size(); ++i) {
        auto t = triangles[i];
        std::vector<Point2D> tris{toPx(axes, rect, vertices[t[0]]),
                                  toPx(axes, rect, vertices[t[1]]),
                                  toPx(axes, rect, vertices[t[2]])};
        if (!ring.empty())
            tris = clipTrianglesToPolygon(tris, ring);
        if (!tris.empty())
            spine.drawTriangles(cmd, clip, res, tris, at(faceColors, i, def));
    }
}

void TriMeshCollection::contributeToAutoscale(Viewport& v) const {
    for (auto p : vertices) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}

// ═══ path-collection subclasses ═══════════════════════════════════════════

namespace {

std::vector<Point2D> radiiToSizes(const std::vector<float>& radii) {
    std::vector<Point2D> s;
    s.reserve(radii.size());
    for (float r : radii) s.push_back({r, r});
    return s;
}

} // namespace

CircleCollection::CircleCollection(std::vector<float> radii,
                                   std::vector<Point2D> offs)
    : PathCollection(Path::unitCircle(), std::move(offs)) {
    sizes = radiiToSizes(radii);
}

RegularPolyCollection::RegularPolyCollection(int n, std::vector<float> radii,
                                             std::vector<Point2D> offs)
    : PathCollection(Path::unitRegularPolygon(n), std::move(offs)) {
    sizes = radiiToSizes(radii);
}

AsteriskPolygonCollection::AsteriskPolygonCollection(
    int n, std::vector<float> radii, std::vector<Point2D> offs)
    : PathCollection(Path::unitAsterisk(n), std::move(offs)) {
    sizes = radiiToSizes(radii);
    strokeOnly = true;
}

// ═══ Vector emit (§17: PDF/SVG/EPS/PGF export) ═════════════════════════════

void Collection::emitSubpaths(render::VectorCanvas& c,
                              const std::vector<Path::Subpath>& subs,
                              Color face, Color edge, float lw,
                              std::span<const float> dash,
                              const std::string& hatchStr,
                              float hatchSpacing,
                              std::span<const Point2D> clipRing) {
    if (face.a > 0.0f) {
        for (const auto& sp : subs) {
            if (!sp.closed || sp.points.size() < 3) continue;
            if (clipRing.empty()) {
                c.polygon(sp.points, face);
            } else {
                auto tris = clipTrianglesToPolygon(
                    earClip(sp.points), clipRing);
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    Point2D t[3] = {tris[i], tris[i+1], tris[i+2]};
                    c.polygon(t, face);
                }
            }
        }
        if (!hatchStr.empty()) {
            render::VectorCanvas::Pen pen;
            pen.color = edge.a > 0 ? edge : Color::black();
            pen.width = std::max(0.6f, hatchSpacing * 0.1f);
            for (const auto& sp : subs) {
                if (!sp.closed || sp.points.size() < 3) continue;
                auto tris = hatchTriangles(sp.points, hatchStr, hatchSpacing);
                if (!clipRing.empty())
                    tris = clipTrianglesToPolygon(tris, clipRing);
                // hatchTriangles returns a triangle soup; emit as quads→
                // polygons per triangle.
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    Point2D t[3] = {tris[i], tris[i+1], tris[i+2]};
                    c.polygon(t, pen.color);
                }
            }
        }
    }
    if (edge.a > 0.0f && lw > 0.0f) {
        render::VectorCanvas::Pen pen;
        pen.color = edge; pen.width = lw;
        pen.dashes.assign(dash.begin(), dash.end());
        for (const auto& sp : subs) {
            if (sp.points.size() < 2) continue;
            std::vector<Point2D> pts = sp.points;
            if (sp.closed) pts.push_back(pts.front());
            if (clipRing.empty()) {
                c.polyline(pts, pen);
            } else {
                for (auto& piece : clipPolylineToPolygon(pts, clipRing))
                    c.polyline(piece, pen);
            }
        }
    }
}

namespace {

/// Shift every point of every subpath (pixel space).
void shiftSubs(std::vector<Path::Subpath>& subs, Point2D off) {
    for (auto& sp : subs)
        for (auto& pt : sp.points) { pt.x += off.x; pt.y += off.y; }
}

/// Per-pass color/width resolution for a path-effect draw. mpl:
///   Stroke       → gc overrides (foreground/linewidth), face kept
///   PatchShadow  → fill-only copy, face = shadow_rgbFace or rgbFace*rho
///   LineShadow   → stroke-only copy, edge = shadow_color or fg*rho
struct FxPass { Color face, edge; float lw; };

FxPass fxPass(const PathEffect& e, Color face, Color edge, float lw,
              float dpi) {
    switch (e.kind) {
    case PathEffect::Kind::Stroke: {
        // mpl _update_gc: foreground/linewidth/alpha overrides.
        Color f = face, ed = e.foreground.value_or(edge);
        if (e.alpha) { f.a = *e.alpha; ed.a = *e.alpha; }
        return {f, ed, e.strokeWidthPx(lw, dpi)};
    }
    case PathEffect::Kind::PatchShadow: {
        // mpl: (rgbFace or (1,1,1)) * rho; stroke suppressed.
        Color base = face.a > 0 ? face : Color::white();
        Color shadow = e.shadowColor.value_or(
            Color{base.r * e.rho, base.g * e.rho, base.b * e.rho, 1.0f});
        shadow.a = e.shadowAlpha;
        return {shadow, Color::transparent(), 0.0f};
    }
    case PathEffect::Kind::LineShadow: {
        // mpl: shadow_color ('k' default) or gc foreground × rho.
        Color shadow = e.shadowColor.value_or(
            Color{edge.r * e.rho, edge.g * e.rho, edge.b * e.rho, 1.0f});
        shadow.a = e.shadowAlpha;
        return {Color::transparent(), shadow,
                e.strokeWidthPx(lw, dpi)};
    }
    case PathEffect::Kind::Normal: break;
    }
    return {face, edge, lw};
}

} // namespace

void Collection::drawSubpathsFx(vk::CommandBuffer cmd,
                                render::Renderer& r,
                                vk::Rect2D clip, vk::Extent2D res,
                                std::vector<Path::Subpath> subs,
                                Color face, Color edge, float lw,
                                std::span<const float> dash,
                                const std::string& hatch, float hatchSpacing,
                                float sketchScale, float sketchLength,
                                std::span<const Point2D> clipRing,
                                std::span<const PathEffect> fx, float dpi) {
    if (fx.empty()) {
        drawSubpaths(cmd, r, clip, res, subs, face, edge, lw, dash, hatch,
                     hatchSpacing, sketchScale, sketchLength, clipRing);
        return;
    }
    for (const auto& e : fx) {
        auto pass = subs;
        shiftSubs(pass, e.offsetPx(dpi));
        auto [f2, e2, lw2] = fxPass(e, face, edge, lw, dpi);
        bool shadow = e.kind == PathEffect::Kind::PatchShadow ||
                      e.kind == PathEffect::Kind::LineShadow;
        drawSubpaths(cmd, r, clip, res, pass, f2, e2, lw2, dash,
                     shadow ? "" : hatch, hatchSpacing,
                     sketchScale, sketchLength, clipRing);
        if (e.thenNormal)
            drawSubpaths(cmd, r, clip, res, subs, face, edge, lw, dash,
                         hatch, hatchSpacing, sketchScale, sketchLength,
                         clipRing);
    }
}

void Collection::emitSubpathsFx(render::VectorCanvas& c,
                                std::vector<Path::Subpath> subs,
                                Color face, Color edge, float lw,
                                std::span<const float> dash,
                                const std::string& hatch, float hatchSpacing,
                                std::span<const Point2D> clipRing,
                                std::span<const PathEffect> fx, float dpi) {
    if (fx.empty()) {
        emitSubpaths(c, subs, face, edge, lw, dash, hatch, hatchSpacing,
                     clipRing);
        return;
    }
    for (const auto& e : fx) {
        auto pass = subs;
        shiftSubs(pass, e.offsetPx(dpi));
        auto [f2, e2, lw2] = fxPass(e, face, edge, lw, dpi);
        bool noHatch = e.kind == PathEffect::Kind::PatchShadow ||
                       e.kind == PathEffect::Kind::LineShadow;
        emitSubpaths(c, pass, f2, e2, lw2,
                     e.kind == PathEffect::Kind::Stroke ? dash
                                                        : std::span<const float>{},
                     noHatch ? "" : hatch, hatchSpacing, clipRing);
        if (e.thenNormal)
            emitSubpaths(c, subs, face, edge, lw, dash, hatch, hatchSpacing,
                         clipRing);
    }
}

void PatchCollection::emitVector(render::VectorCanvas& c, const Axes& axes,
                                 Rect2D rect) {
    auto ring = clipRingPx(axes, rect);
    for (const auto& p : patches) {
        Path path = p.style.transform ? p.path.transformed(*p.style.transform)
                                      : p.path;
        auto subs = path.toPolylines();
        for (auto& sp : subs)
            for (auto& pt : sp.points) pt = toPx(axes, rect, pt);
        std::vector<float> dash = p.style.dashes;
        if (dash.empty())
            dash = dashPattern(p.style.lineStyle, p.style.lineWidth);
        emitSubpathsFx(c, subs, p.style.face, p.style.edge,
                       p.style.lineWidth, dash, p.style.hatch,
                       p.style.hatchSpacing, ring, pathEffects,
                       axes.style().dpi);
    }
}

void PathCollection::emitVector(render::VectorCanvas& c, const Axes& axes,
                                Rect2D rect) {
    if (offsets.empty()) return;
    auto ring = clipRingPx(axes, rect);
    auto proto = path.toPolylines();
    Color defFace = faceColors.empty() ? Color{0.121f, 0.466f, 0.705f, 1}
                                       : Color{};
    Color defEdge = edgeColors.empty() ? Color{0, 0, 0, 0} : Color{};
    float ppu = float(rect.width) /
                std::max(1e-9f, axes.viewport().x.span());
    float ppv = float(rect.height) /
                std::max(1e-9f, axes.viewport().y.span());
    for (size_t i = 0; i < offsets.size(); ++i) {
        Point2D size = at(sizes, i, Point2D{1, 1});
        Point2D center = offsetPx(axes, rect, offsetTransform.get(),
                                  offsets[i], {});
        auto subs = proto;
        for (auto& sp : subs)
            for (auto& pt : sp.points) {
                Point2D scaled{pt.x * size.x, pt.y * size.y};
                if (i < transforms.size() && transforms[i])
                    scaled = transforms[i]->apply(scaled);
                pt = {center.x + scaled.x * ppu, center.y - scaled.y * ppv};
            }
        float lw = at(lineWidths, i, 1.0f);
        std::vector<float> dash =
            dashes.empty() ? dashPattern(lineStyle, lw) : dashes;
        Color face = at(faceColors, i, defFace);
        if (strokeOnly) face.a = 0.0f;
        emitSubpathsFx(c, subs, face, at(edgeColors, i, defEdge), lw, dash,
                       hatch, hatchSpacing, ring, pathEffects,
                       axes.style().dpi);
    }
}

void LineCollection::emitVector(render::VectorCanvas& c, const Axes& axes,
                                Rect2D rect) {
    Color def{0.121f, 0.466f, 0.705f, 1};
    auto ring = clipRingPx(axes, rect);
    const float dpi = axes.style().dpi;
    for (size_t i = 0; i < segments.size(); ++i) {
        std::vector<Point2D> px;
        px.reserve(segments[i].size());
        for (auto p : segments[i]) px.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.5f);
        Color col = at(edgeColors, i, def);
        std::vector<float> dash =
            dashes.empty() ? dashPattern(lineStyle, lw) : dashes;
        auto pass = [&](Point2D off, Color pc, float plw) {
            std::vector<Point2D> pxs = px;
            for (auto& q : pxs) { q.x += off.x; q.y += off.y; }
            std::vector<std::vector<Point2D>> pieces{std::move(pxs)};
            if (!ring.empty())
                pieces = clipPolylineToPolygon(pieces[0], ring);
            if (pieces.empty() || pc.a <= 0) return;
            render::VectorCanvas::Pen pen;
            pen.color = pc; pen.width = plw;
            pen.dashes = dash;
            for (auto& piece : pieces) c.polyline(piece, pen);
        };
        if (pathEffects.empty()) {
            pass({0.0f, 0.0f}, col, lw);
            continue;
        }
        for (const auto& e : pathEffects) {
            Color pc = col; float plw = lw;
            switch (e.kind) {
            case PathEffect::Kind::Stroke:
                pc = e.foreground.value_or(col);
                plw = e.strokeWidthPx(lw, dpi);
                break;
            case PathEffect::Kind::LineShadow:
            case PathEffect::Kind::PatchShadow:
                pc = e.shadowColor.value_or(
                    Color{col.r * e.rho, col.g * e.rho, col.b * e.rho,
                          1.0f});
                pc.a = e.shadowAlpha;
                plw = e.strokeWidthPx(lw, dpi);
                break;
            case PathEffect::Kind::Normal: break;
            }
            pass(e.offsetPx(dpi), pc, plw);
            if (e.thenNormal) pass({0.0f, 0.0f}, col, lw);
        }
    }
}

void PolyCollection::emitVector(render::VectorCanvas& c, const Axes& axes,
                                Rect2D rect) {
    auto ring = clipRingPx(axes, rect);
    Color defFace{0.121f, 0.466f, 0.705f, 1};
    Color defEdge{0, 0, 0, 0};
    for (size_t i = 0; i < polys.size(); ++i) {
        Path::Subpath sp;
        sp.closed = true;
        sp.points.reserve(polys[i].size());
        for (auto p : polys[i]) sp.points.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.0f);
        std::vector<float> dash = dashes.empty() ? dashPattern(lineStyle, lw)
                                                 : dashes;
        emitSubpathsFx(c, {sp}, at(faceColors, i, defFace),
                       at(edgeColors, i, defEdge), lw, dash, hatch,
                       hatchSpacing, ring, pathEffects, axes.style().dpi);
    }
}

void QuadMesh::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (rows == 0 || cols == 0 ||
        corners.size() < size_t(rows + 1) * (cols + 1))
        return;
    auto ring = clipRingPx(axes, rect);
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t col = 0; col < cols; ++col) {
            size_t i = size_t(row) * cols + col;
            Point2D q[4] = {
                toPx(axes, rect, corners[size_t(row) * (cols + 1) + col]),
                toPx(axes, rect, corners[size_t(row) * (cols + 1) + col + 1]),
                toPx(axes, rect,
                     corners[size_t(row + 1) * (cols + 1) + col + 1]),
                toPx(axes, rect, corners[size_t(row + 1) * (cols + 1) + col])};
            if (ring.empty()) {
                c.polygon(q, at(cellColors, i, def));
            } else {
                std::vector<Point2D> tris{q[0], q[1], q[2],
                                          q[0], q[2], q[3]};
                tris = clipTrianglesToPolygon(tris, ring);
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    Point2D t[3] = {tris[i], tris[i+1], tris[i+2]};
                    c.polygon(t, at(cellColors, i, def));
                }
            }
        }
}

void TriMeshCollection::emitVector(render::VectorCanvas& c, const Axes& axes,
                                   Rect2D rect) {
    auto ring = clipRingPx(axes, rect);
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (size_t i = 0; i < triangles.size(); ++i) {
        auto t = triangles[i];
        Point2D tri[3] = {toPx(axes, rect, vertices[t[0]]),
                          toPx(axes, rect, vertices[t[1]]),
                          toPx(axes, rect, vertices[t[2]])};
        if (ring.empty()) {
            c.polygon(tri, at(faceColors, i, def));
        } else {
            std::vector<Point2D> tris{tri[0], tri[1], tri[2]};
            tris = clipTrianglesToPolygon(tris, ring);
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                Point2D t3[3] = {tris[i], tris[i+1], tris[i+2]};
                c.polygon(t3, at(faceColors, i, def));
            }
        }
    }
}

// ═══ Picking (§16) ════════════════════════════════════════════════════════

bool PatchCollection::contains(const Axes&, Point2D pt) const {
    for (const auto& p : patches)
        if (p.path.containsPoint(pt)) return true;
    return false;
}

bool PolyCollection::contains(const Axes&, Point2D pt) const {
    // Ray casting per polygon.
    for (const auto& poly : polys) {
        if (poly.size() < 3) continue;
        bool inside = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            const auto& a = poly[i];
            const auto& b = poly[j];
            if ((a.y > pt.y) != (b.y > pt.y) &&
                pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        if (inside) return true;
    }
    return false;
}

} // namespace volcano::plot
