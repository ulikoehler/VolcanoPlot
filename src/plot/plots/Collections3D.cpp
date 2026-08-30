// volcano/plot/plots/Collections3D.cpp — 3D line and polygon collections
#include "volcano/plot/plots/Collections3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

Point2D project(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
}

float projectDepth(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipZ = vp[8]*x + vp[9]*y + vp[10]*z + vp[11];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return 0.0f;
    return clipZ / clipW;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Line3DCollection
// ═══════════════════════════════════════════════════════════════════════════

Line3DCollection::Line3DCollection(std::vector<float> segments,
                                   Line3DCollectionConfig config)
    : config_(std::move(config)) {
    if (segments.size() % 6 != 0)
        throw std::invalid_argument("Line3DCollection: segments size must be a multiple of 6");
    size_t n = segments.size() / 6;
    segments_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Point3D p0{segments[i*6+0], segments[i*6+1], segments[i*6+2]};
        Point3D p1{segments[i*6+3], segments[i*6+4], segments[i*6+5]};
        segments_.push_back({p0, p1});
    }
}

Line3DCollection::Line3DCollection(std::vector<std::pair<Point3D, Point3D>> segments,
                                   Line3DCollectionConfig config)
    : segments_(std::move(segments)), config_(std::move(config)) {}

void Line3DCollection::projectSegments() {
    projected_.clear();
    segmentColors_.clear();

    if (segments_.empty()) return;

    auto vp = camera_.viewProjection();

    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& [p0, p1] = segments_[i];
        projected_.push_back(project(vp, p0.x, p0.y, p0.z));
        projected_.push_back(project(vp, p1.x, p1.y, p1.z));
        if (i < config_.colors.size())
            segmentColors_.push_back(config_.colors[i]);
        else
            segmentColors_.push_back(config_.color);
    }
}

void Line3DCollection::prepare(render::Renderer& r) {
    projectSegments();

    auto& ctx = r.backend().context();

    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!projected_.empty()) {
        // LineSegmentRenderer uses a single color for all segments.
        // For per-segment colors, we'd need a different renderer, but
        // the current LineSegmentRenderer only supports one color.
        // We use the default color; per-segment colors are stored but
        // not used by the current renderer.
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{projected_}, config_.color,
                         config_.lineWidth);
    }

    prepared_ = true;
}

void Line3DCollection::draw(vk::CommandBuffer cmd, render::Renderer&,
                            const Axes& axes, Rect2D rect) {
    if (!prepared_ || projected_.empty()) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    renderer_.draw(cmd, vrect, t, static_cast<uint32_t>(projected_.size()));
}

void Line3DCollection::contributeToAutoscale(Viewport& v) const {
    for (const auto& [p0, p1] : segments_) {
        v.x.min = std::min(v.x.min, p0.x);
        v.x.max = std::max(v.x.max, p0.x);
        v.y.min = std::min(v.y.min, p0.y);
        v.y.max = std::max(v.y.max, p0.y);
        v.z.min = std::min(v.z.min, p0.z);
        v.z.max = std::max(v.z.max, p0.z);
        v.x.min = std::min(v.x.min, p1.x);
        v.x.max = std::max(v.x.max, p1.x);
        v.y.min = std::min(v.y.min, p1.y);
        v.y.max = std::max(v.y.max, p1.y);
        v.z.min = std::min(v.z.min, p1.z);
        v.z.max = std::max(v.z.max, p1.z);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Poly3DCollection
// ═══════════════════════════════════════════════════════════════════════════

Poly3DCollection::Poly3DCollection(std::vector<std::vector<Point3D>> polygons,
                                   Poly3DCollectionConfig config)
    : polygons_(std::move(polygons)), config_(std::move(config)) {}

void Poly3DCollection::projectPolygons() {
    fillPositions_.clear();
    fillColors_.clear();
    edgeSegments_.clear();

    if (polygons_.empty()) return;

    auto vp = camera_.viewProjection();

    struct ProjectedPoly {
        std::vector<Point2D> verts;
        float depth;
        Color color;
    };

    std::vector<ProjectedPoly> projectedPolys;

    for (size_t i = 0; i < polygons_.size(); ++i) {
        const auto& poly = polygons_[i];
        if (poly.size() < 3) continue;

        ProjectedPoly pp;
        pp.verts.reserve(poly.size());
        float avgDepth = 0.0f;
        for (const auto& v : poly) {
            pp.verts.push_back(project(vp, v.x, v.y, v.z));
            avgDepth += projectDepth(vp, v.x, v.y, v.z);
        }
        pp.depth = avgDepth / static_cast<float>(poly.size());
        pp.color = (i < config_.faceColors.size()) ? config_.faceColors[i] : config_.faceColor;
        projectedPolys.push_back(std::move(pp));
    }

    // Sort polygons back-to-front (painter's algorithm).
    std::sort(projectedPolys.begin(), projectedPolys.end(),
              [](const ProjectedPoly& a, const ProjectedPoly& b) {
                  return a.depth > b.depth;
              });

    // Build fill triangles (fan triangulation) and edge segments.
    for (const auto& pp : projectedPolys) {
        if (config_.drawFaces) {
            // Fan triangulation: (v0, vi, vi+1) for i = 1..n-2.
            for (size_t i = 1; i + 1 < pp.verts.size(); ++i) {
                fillPositions_.push_back(pp.verts[0]);
                fillPositions_.push_back(pp.verts[i]);
                fillPositions_.push_back(pp.verts[i + 1]);
                for (int k = 0; k < 3; ++k) fillColors_.push_back(pp.color);
            }
        }

        if (config_.drawEdges) {
            // Edge loop: connect consecutive vertices, close the loop.
            for (size_t i = 0; i < pp.verts.size(); ++i) {
                edgeSegments_.push_back(pp.verts[i]);
                edgeSegments_.push_back(pp.verts[(i + 1) % pp.verts.size()]);
            }
        }
    }
}

void Poly3DCollection::prepare(render::Renderer& r) {
    projectPolygons();

    auto& ctx = r.backend().context();

    if (config_.drawFaces && !fillPositions_.empty()) {
        fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }

    if (config_.drawEdges && !edgeSegments_.empty()) {
        edgeRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        edgeRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{edgeSegments_}, config_.edgeColor,
                             config_.edgeWidth);
    }

    prepared_ = true;
}

void Poly3DCollection::draw(vk::CommandBuffer cmd, render::Renderer&,
                            const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (config_.drawFaces && !fillPositions_.empty())
        fillRenderer_.draw(cmd, vrect, t);

    if (config_.drawEdges && !edgeSegments_.empty())
        edgeRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(edgeSegments_.size()));
}

void Poly3DCollection::contributeToAutoscale(Viewport& v) const {
    for (const auto& poly : polygons_) {
        for (const auto& p : poly) {
            v.x.min = std::min(v.x.min, p.x);
            v.x.max = std::max(v.x.max, p.x);
            v.y.min = std::min(v.y.min, p.y);
            v.y.max = std::max(v.y.max, p.y);
            v.z.min = std::min(v.z.min, p.z);
            v.z.max = std::max(v.z.max, p.z);
        }
    }
}

} // namespace volcano::plot
