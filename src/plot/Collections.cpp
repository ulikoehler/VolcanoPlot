// volcano/plot/Collections.cpp — patch primitives + collection rendering
#include "volcano/plot/Collections.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace volcano::plot {

// ═══ patch primitives ═════════════════════════════════════════════════════

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

void Collection::drawSubpaths(vk::CommandBuffer cmd, render::Renderer& r,
                              vk::Rect2D clip, vk::Extent2D res,
                              const std::vector<Path::Subpath>& subs,
                              Color face, Color edge, float lw,
                              std::span<const float> dash,
                              const std::string& hatchStr,
                              float hatchSpacing, float sketchScale) {
    auto& spine = r.spineRenderer();
    // Fill (ear-clipped per closed subpath).
    if (face.a > 0.0f) {
        std::vector<Point2D> tris;
        for (const auto& sp : subs) {
            if (!sp.closed || sp.points.size() < 3) continue;
            auto t = earClip(sp.points);
            tris.insert(tris.end(), t.begin(), t.end());
        }
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
                pts = sketchPolyline(pts, sketchScale * 2.0f);
            appendStroke(ev, pts, sp.closed, lw, dash, 0.0f);
        }
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
    for (const auto& p : patches) {
        Path path = p.style.transform ? p.path.transformed(*p.style.transform)
                                      : p.path;
        auto subs = path.toPolylines();
        for (auto& sp : subs)
            for (auto& pt : sp.points) pt = toPx(axes, rect, pt);
        std::vector<float> dash = p.style.dashes;
        if (dash.empty())
            dash = dashPattern(p.style.lineStyle, p.style.lineWidth);
        drawSubpaths(cmd, r, clip, res, subs, p.style.face, p.style.edge,
                     p.style.lineWidth, dash, p.style.hatch,
                     p.style.hatchSpacing, axes.style().sketchScale);
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
        drawSubpaths(cmd, r, clip, res, subs, face,
                     at(edgeColors, i, defEdge), lw, dash, hatch,
                     hatchSpacing, axes.style().sketchScale);
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
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (size_t i = 0; i < segments.size(); ++i) {
        std::vector<Point2D> px;
        px.reserve(segments[i].size());
        for (auto p : segments[i]) px.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.5f);
        std::vector<float> dash = dashes.empty() ? dashPattern(lineStyle, lw)
                                                 : dashes;
        std::vector<Point2D> ev;
        appendStroke(ev, px, false, lw, dash, 0.0f);
        Color c = at(edgeColors, i, def);
        if (!ev.empty() && c.a > 0)
            spine.drawTriangles(cmd, clip, res, ev, c);
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
    for (size_t i = 0; i < polys.size(); ++i) {
        Path::Subpath sp;
        sp.closed = true;
        sp.points.reserve(polys[i].size());
        for (auto p : polys[i]) sp.points.push_back(toPx(axes, rect, p));
        float lw = at(lineWidths, i, 1.0f);
        std::vector<float> dash = dashes.empty() ? dashPattern(lineStyle, lw)
                                                 : dashes;
        drawSubpaths(cmd, r, clip, res, {sp},
                     at(faceColors, i, defFace), at(edgeColors, i, defEdge),
                     lw, dash, hatch, hatchSpacing, axes.style().sketchScale);
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
    Color def{0.121f, 0.466f, 0.705f, 1};
    for (size_t i = 0; i < triangles.size(); ++i) {
        auto t = triangles[i];
        std::vector<Point2D> tris{toPx(axes, rect, vertices[t[0]]),
                                  toPx(axes, rect, vertices[t[1]]),
                                  toPx(axes, rect, vertices[t[2]])};
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
