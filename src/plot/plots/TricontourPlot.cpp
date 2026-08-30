// volcano/plot/plots/TricontourPlot.cpp — 3D tricontour/tricontourf implementation
#include "volcano/plot/plots/TricontourPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

Point2D project3D(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
}

std::vector<float> autoLevels(float vmin, float vmax, int n) {
    if (n < 2) n = 2;
    if (vmax <= vmin) vmax = vmin + 1.0f;
    std::vector<float> levels(n);
    for (int i = 0; i < n; ++i)
        levels[i] = vmin + (vmax - vmin) * i / (n - 1);
    return levels;
}

float valueRangeMin(const std::vector<float>& z) {
    float vmin = std::numeric_limits<float>::max();
    for (float v : z) if (!std::isnan(v)) vmin = std::min(vmin, v);
    return vmin;
}

float valueRangeMax(const std::vector<float>& z) {
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : z) if (!std::isnan(v)) vmax = std::max(vmax, v);
    return vmax;
}

/// Interpolate where `level` crosses an edge from (p0, v0) to (p1, v1).
Point2D interpCross(Point2D p0, float v0, Point2D p1, float v1, float level) {
    float t = (level - v0) / (v1 - v0);
    return {p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y)};
}

// Sutherland-Hodgman clipping vertex with scalar value.
struct ClipV { Point2D pos; float val; };

std::vector<ClipV> clipAbove(std::span<const ClipV> poly, float level) {
    std::vector<ClipV> out;
    if (poly.empty()) return out;
    auto n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& cur = poly[i];
        const auto& nxt = poly[(i + 1) % n];
        bool curIn = cur.val >= level;
        bool nxtIn = nxt.val >= level;
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) {
            float t = (level - cur.val) / (nxt.val - cur.val);
            out.push_back({{cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                            cur.pos.y + t * (nxt.pos.y - cur.pos.y)}, level});
        }
    }
    return out;
}

std::vector<ClipV> clipBelow(std::span<const ClipV> poly, float level) {
    std::vector<ClipV> out;
    if (poly.empty()) return out;
    auto n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& cur = poly[i];
        const auto& nxt = poly[(i + 1) % n];
        bool curIn = cur.val <= level;
        bool nxtIn = nxt.val <= level;
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) {
            float t = (level - cur.val) / (nxt.val - cur.val);
            out.push_back({{cur.pos.x + t * (nxt.pos.x - cur.pos.x),
                            cur.pos.y + t * (nxt.pos.y - cur.pos.y)}, level});
        }
    }
    return out;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TricontourPlot — contour lines on triangulated data
// ═══════════════════════════════════════════════════════════════════════════

TricontourPlot::TricontourPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> z, TricontourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TricontourPlot: x, y, z must have the same size");
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i) pts[i] = {x_[i], y_[i]};
    triangles_ = delaunay(pts);
}

TricontourPlot::TricontourPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> z,
                               std::vector<Triangle> triangles,
                               TricontourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      triangles_(std::move(triangles)), config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TricontourPlot: x, y, z must have the same size");
}

void TricontourPlot::computeLevels() {
    if (!config_.levels.empty()) return;
    float vmin = valueRangeMin(z_);
    float vmax = valueRangeMax(z_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void TricontourPlot::extractContours() {
    segments_.clear();
    if (triangles_.empty() || x_.empty()) return;

    float vmin = valueRangeMin(z_);
    float zLevel = config_.zOffset ? (vmin + config_.zLevel) : config_.zLevel;
    auto vp = camera_.viewProjection();

    for (const auto& tri : triangles_) {
        if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
            continue;

        Point2D p0{x_[tri.a], y_[tri.a]}, p1{x_[tri.b], y_[tri.b]}, p2{x_[tri.c], y_[tri.c]};
        float v0 = z_[tri.a], v1 = z_[tri.b], v2 = z_[tri.c];

        for (float level : config_.levels) {
            // Count edge crossings.
            bool e01 = (v0 >= level) != (v1 >= level);
            bool e12 = (v1 >= level) != (v2 >= level);
            bool e20 = (v2 >= level) != (v0 >= level);

            int crossings = e01 + e12 + e20;
            if (crossings != 2) continue;

            // Find the two crossing points.
            Point2D c0{}, c1{};
            int found = 0;
            if (e01) { c0 = interpCross(p0, v0, p1, v1, level); ++found; }
            if (e12) { (found == 0 ? c0 : c1) = interpCross(p1, v1, p2, v2, level); ++found; }
            if (e20) { c1 = interpCross(p2, v2, p0, v0, level); ++found; }

            if (found == 2) {
                segments_.push_back(project3D(vp, c0.x, c0.y, zLevel));
                segments_.push_back(project3D(vp, c1.x, c1.y, zLevel));
            }
        }
    }
}

void TricontourPlot::prepare(render::Renderer& r) {
    computeLevels();
    extractContours();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!segments_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{segments_}, config_.lineColor,
                         config_.lineWidth);
    }
    prepared_ = true;
}

void TricontourPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));
}

void TricontourPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TricontourfPlot — filled contour bands on triangulated data
// ═══════════════════════════════════════════════════════════════════════════

TricontourfPlot::TricontourfPlot(std::vector<float> x, std::vector<float> y,
                                 std::vector<float> z, TricontourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TricontourfPlot: x, y, z must have the same size");
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i) pts[i] = {x_[i], y_[i]};
    triangles_ = delaunay(pts);
}

TricontourfPlot::TricontourfPlot(std::vector<float> x, std::vector<float> y,
                                 std::vector<float> z,
                                 std::vector<Triangle> triangles,
                                 TricontourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      triangles_(std::move(triangles)), config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TricontourfPlot: x, y, z must have the same size");
}

Color TricontourfPlot::legendColor() const {
    if (config_.cmap) return config_.cmap->sample(0.5f);
    return Color::fromRgba8(128, 128, 128, 255);
}

void TricontourfPlot::computeLevels() {
    if (!config_.levels.empty()) return;
    float vmin = valueRangeMin(z_);
    float vmax = valueRangeMax(z_);
    config_.levels = autoLevels(vmin, vmax, config_.numLevels);
}

void TricontourfPlot::extractContoursFilled() {
    positions_.clear();
    colors_.clear();
    if (triangles_.empty() || x_.empty()) return;

    float vmin = valueRangeMin(z_);
    float vmax = valueRangeMax(z_);
    float vrange = vmax - vmin;
    if (vrange <= 0.0f) vrange = 1.0f;

    float zLevel = config_.zOffset ? (vmin + config_.zLevel) : config_.zLevel;
    auto vp = camera_.viewProjection();

    const auto& L = config_.levels;
    int numBands = static_cast<int>(L.size()) - 1;

    for (const auto& tri : triangles_) {
        if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
            continue;

        ClipV cell[] = {
            {{x_[tri.a], y_[tri.a]}, z_[tri.a]},
            {{x_[tri.b], y_[tri.b]}, z_[tri.b]},
            {{x_[tri.c], y_[tri.c]}, z_[tri.c]},
        };

        float cellMin = std::min({z_[tri.a], z_[tri.b], z_[tri.c]});
        float cellMax = std::max({z_[tri.a], z_[tri.b], z_[tri.c]});

        for (int b = 0; b <= numBands; ++b) {
            float lo = (b == 0) ? (vmin - 1.0f) : L[b - 1];
            float hi = (b == numBands) ? (vmax + 1.0f) : L[b];
            if (cellMax < lo || cellMin > hi) continue;

            auto poly = clipAbove(cell, lo);
            poly = clipBelow(poly, hi);
            if (poly.size() < 3) continue;

            float mid = (lo + hi) * 0.5f;
            float t = std::clamp((mid - vmin) / vrange, 0.0f, 1.0f);
            Color color = config_.cmap ? config_.cmap->sample(t)
                                       : Color::fromRgba8(
                                             static_cast<uint8_t>(255 * t),
                                             static_cast<uint8_t>(255 * t),
                                             static_cast<uint8_t>(255 * t));

            for (size_t k = 1; k + 1 < poly.size(); ++k) {
                positions_.push_back(project3D(vp, poly[0].pos.x, poly[0].pos.y, zLevel));
                positions_.push_back(project3D(vp, poly[k].pos.x, poly[k].pos.y, zLevel));
                positions_.push_back(project3D(vp, poly[k+1].pos.x, poly[k+1].pos.y, zLevel));
                for (int c = 0; c < 3; ++c) colors_.push_back(color);
            }
        }
    }
}

void TricontourfPlot::prepare(render::Renderer& r) {
    computeLevels();
    extractContoursFilled();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!positions_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{positions_}, std::span{colors_});
    }
    prepared_ = true;
}

void TricontourfPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                           const Axes& axes, Rect2D rect) {
    if (!prepared_ || positions_.empty()) return;
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void TricontourfPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
