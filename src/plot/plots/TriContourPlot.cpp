// volcano/plot/plots/TriContourPlot.cpp — tricontour and tricontourf
#include "volcano/plot/plots/TriContourPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

/// Linear interpolation of where a contour crosses an edge.
/// Returns the point where z = level between p0 and p1.
Point2D interpEdge(Point2D p0, float z0, Point2D p1, float z1, float level) {
    float denom = z1 - z0;
    if (std::abs(denom) < 1e-20f) return {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    float t = (level - z0) / denom;
    return {p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y)};
}

/// Compute auto levels from data range.
std::vector<float> autoLevels(float vmin, float vmax, int n) {
    if (vmin >= vmax) { vmax = vmin + 1.0f; }
    std::vector<float> levels(n);
    for (int i = 0; i < n; ++i)
        levels[i] = vmin + (vmax - vmin) * i / (n - 1);
    return levels;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TriContourPlot (tricontour — isolines)
// ═══════════════════════════════════════════════════════════════════════════

TriContourPlot::TriContourPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> z, TriContourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TriContourPlot: x, y, z must have the same size");
    if (x_.size() < 3)
        throw std::invalid_argument("TriContourPlot: need at least 3 points");
}

void TriContourPlot::computeLevels() {
    if (!config_.levels.empty()) {
        levels_ = config_.levels;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : z_) {
        if (std::isnan(v)) continue;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (vmin >= vmax) { vmin = 0.0f; vmax = 1.0f; }
    levels_ = autoLevels(vmin, vmax, config_.numLevels);
}

void TriContourPlot::marchingTriangles() {
    segments_.clear();

    for (size_t li = 0; li < levels_.size(); ++li) {
        float level = levels_[li];

        for (const auto& tri : tris_) {
            Point2D p0{x_[tri.a], y_[tri.a]};
            Point2D p1{x_[tri.b], y_[tri.b]};
            Point2D p2{x_[tri.c], y_[tri.c]};
            float z0 = z_[tri.a], z1 = z_[tri.b], z2 = z_[tri.c];

            // Skip if any z is NaN.
            if (std::isnan(z0) || std::isnan(z1) || std::isnan(z2)) continue;

            // Count how many vertices are above the level.
            bool a0 = z0 >= level;
            bool a1 = z1 >= level;
            bool a2 = z2 >= level;
            int above = (a0 ? 1 : 0) + (a1 ? 1 : 0) + (a2 ? 1 : 0);

            // No crossing or all above/below.
            if (above == 0 || above == 3) continue;

            // Find the two edges that cross the level.
            Point2D crossings[2];
            int ci = 0;
            if (a0 != a1) crossings[ci++] = interpEdge(p0, z0, p1, z1, level);
            if (a1 != a2) crossings[ci++] = interpEdge(p1, z1, p2, z2, level);
            if (a2 != a0 && ci < 2) crossings[ci++] = interpEdge(p2, z2, p0, z0, level);

            if (ci == 2) {
                segments_.push_back(crossings[0]);
                segments_.push_back(crossings[1]);
            }
        }
    }
}

void TriContourPlot::prepare(render::Renderer& r) {
    // Triangulate.
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i)
        pts[i] = {x_[i], y_[i]};
    tris_ = delaunay(pts);

    computeLevels();
    marchingTriangles();

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

void TriContourPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));
}

void TriContourPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TriContourfPlot (tricontourf — filled bands)
// ═══════════════════════════════════════════════════════════════════════════

TriContourfPlot::TriContourfPlot(std::vector<float> x, std::vector<float> y,
                                 std::vector<float> z, TriContourConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TriContourfPlot: x, y, z must have the same size");
    if (x_.size() < 3)
        throw std::invalid_argument("TriContourfPlot: need at least 3 points");
}

Color TriContourfPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void TriContourfPlot::computeLevels() {
    if (!config_.levels.empty()) {
        levels_ = config_.levels;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : z_) {
        if (std::isnan(v)) continue;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (vmin >= vmax) { vmin = 0.0f; vmax = 1.0f; }
    levels_ = autoLevels(vmin, vmax, config_.numLevels);
}

void TriContourfPlot::marchingTrianglesFilled() {
    positions_.clear();
    colors_.clear();

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vmin = levels_.front();
    float vmax = levels_.back();
    float vspan = vmax - vmin;
    if (vspan <= 0.0f) vspan = 1.0f;

    // For each triangle, clip it against each level band and emit polygons.
    for (const auto& tri : tris_) {
        Point2D p0{x_[tri.a], y_[tri.a]};
        Point2D p1{x_[tri.b], y_[tri.b]};
        Point2D p2{x_[tri.c], y_[tri.c]};
        float z0 = z_[tri.a], z1 = z_[tri.b], z2 = z_[tri.c];

        if (std::isnan(z0) || std::isnan(z1) || std::isnan(z2)) continue;

        // For each band [levels[i], levels[i+1]], clip the triangle.
        for (size_t i = 0; i + 1 < levels_.size(); ++i) {
            float lo = levels_[i];
            float hi = levels_[i + 1];

            // Clip the triangle to the band [lo, hi] using Sutherland-Hodgman
            // against two half-planes: z >= lo and z <= hi.
            // Start with the full triangle.
            std::vector<Point2D> poly = {p0, p1, p2};
            std::vector<float> polyZ = {z0, z1, z2};

            // Clip: z >= lo
            {
                std::vector<Point2D> newPoly;
                std::vector<float> newZ;
                for (size_t j = 0; j < poly.size(); ++j) {
                    size_t k = (j + 1) % poly.size();
                    bool aj = polyZ[j] >= lo;
                    bool ak = polyZ[k] >= lo;
                    if (aj) {
                        newPoly.push_back(poly[j]);
                        newZ.push_back(polyZ[j]);
                    }
                    if (aj != ak) {
                        float t = (lo - polyZ[j]) / (polyZ[k] - polyZ[j]);
                        newPoly.push_back({
                            poly[j].x + t * (poly[k].x - poly[j].x),
                            poly[j].y + t * (poly[k].y - poly[j].y)
                        });
                        newZ.push_back(lo);
                    }
                }
                poly = newPoly;
                polyZ = newZ;
            }

            // Clip: z <= hi
            {
                std::vector<Point2D> newPoly;
                std::vector<float> newZ;
                for (size_t j = 0; j < poly.size(); ++j) {
                    size_t k = (j + 1) % poly.size();
                    bool aj = polyZ[j] <= hi;
                    bool ak = polyZ[k] <= hi;
                    if (aj) {
                        newPoly.push_back(poly[j]);
                        newZ.push_back(polyZ[j]);
                    }
                    if (aj != ak) {
                        float t = (hi - polyZ[j]) / (polyZ[k] - polyZ[j]);
                        newPoly.push_back({
                            poly[j].x + t * (poly[k].x - poly[j].x),
                            poly[j].y + t * (poly[k].y - poly[j].y)
                        });
                        newZ.push_back(hi);
                    }
                }
                poly = newPoly;
                polyZ = newZ;
            }

            if (poly.size() < 3) continue;

            // Color for this band.
            float mid = (lo + hi) * 0.5f;
            Color color = cmap.sample((mid - vmin) / vspan);

            // Tessellate polygon as a fan.
            for (size_t j = 1; j + 1 < poly.size(); ++j) {
                positions_.push_back(poly[0]);
                positions_.push_back(poly[j]);
                positions_.push_back(poly[j + 1]);
                for (int k = 0; k < 3; ++k) colors_.push_back(color);
            }
        }
    }
}

void TriContourfPlot::prepare(render::Renderer& r) {
    // Triangulate.
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i)
        pts[i] = {x_[i], y_[i]};
    tris_ = delaunay(pts);

    computeLevels();
    marchingTrianglesFilled();

    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!positions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{positions_}, std::span{colors_});
    }
    prepared_ = true;
}

void TriContourfPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                           const Axes& axes, Rect2D rect) {
    if (!prepared_ || positions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void TriContourfPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
