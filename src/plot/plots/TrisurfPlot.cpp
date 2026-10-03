// volcano/plot/plots/TrisurfPlot.cpp — 3D triangulated surface implementation
#include "volcano/plot/plots/TrisurfPlot.hpp"
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



} // namespace

TrisurfPlot::TrisurfPlot(std::vector<float> x, std::vector<float> y,
                         std::vector<float> z, TrisurfConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TrisurfPlot: x, y, z must have the same size");
    // Delaunay triangulate (x, y) points.
    std::vector<Point2D> pts(x_.size());
    for (size_t i = 0; i < x_.size(); ++i)
        pts[i] = {x_[i], y_[i]};
    triangles_ = delaunay(pts);
}

TrisurfPlot::TrisurfPlot(std::vector<float> x, std::vector<float> y,
                         std::vector<float> z, std::vector<Triangle> triangles,
                         TrisurfConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      triangles_(std::move(triangles)), config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TrisurfPlot: x, y, z must have the same size");
}

Color TrisurfPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void TrisurfPlot::projectSurface() {
    fillPositions_.clear();
    fillColors_.clear();
    edgeSegments_.clear();

    if (x_.empty() || triangles_.empty()) return;

    vp_ = camera_.viewProjection();
    const auto& vp = vp_;

    // Compute z range for color mapping.
    Range zRange = config_.valueRange;
    if (config_.norm) {
        config_.norm->autoscale(z_);
        zRange = {config_.norm->vmin(), config_.norm->vmax()};
    } else if (!zRange.valid()) {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float v : z_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        if (vmin > vmax) { vmin = 0; vmax = 1; }
        zRange = {vmin, vmax};
    }
    float zSpan = zRange.span();
    if (zSpan <= 0) zSpan = 1;

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();

    // Project each triangle and compute average depth for sorting.
    struct ProjectedTri {
        // Raw world-space verts — projected by the renderer.
        Point3D v0, v1, v2;
        float depth;
        Color color;
    };
    std::vector<ProjectedTri> tris;

    for (const auto& tri : triangles_) {
        if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
            continue;

        Point3D p0{x_[tri.a], y_[tri.a], z_[tri.a]};
        Point3D p1{x_[tri.b], y_[tri.b], z_[tri.b]};
        Point3D p2{x_[tri.c], y_[tri.c], z_[tri.c]};

        float avgDepth = (projectDepth3D(vp, p0) +
                          projectDepth3D(vp, p1) +
                          projectDepth3D(vp, p2)) / 3.0f;

        float avgZ = (p0.z + p1.z + p2.z) / 3.0f;
        float t;
        if (config_.norm) {
            t = (*config_.norm)(avgZ);
        } else {
            t = (avgZ - zRange.min) / zSpan;
        }
        Color color = cmap.sample(t);

        tris.push_back({p0, p1, p2, avgDepth, color});
    }

    // Sort back-to-front (painter's algorithm).
    std::sort(tris.begin(), tris.end(),
              [](const ProjectedTri& a, const ProjectedTri& b) {
                  return a.depth > b.depth;
              });

    // Build fill triangles.
    for (const auto& tri : tris) {
        fillPositions_.push_back(tri.v0);
        fillPositions_.push_back(tri.v1);
        fillPositions_.push_back(tri.v2);
        for (int k = 0; k < 3; ++k) fillColors_.push_back(tri.color);
    }

    // Build edge segments.
    if (config_.drawEdges) {
        for (const auto& tri : triangles_) {
            if (tri.a >= x_.size() || tri.b >= x_.size() || tri.c >= x_.size())
                continue;
            Point3D p0{x_[tri.a], y_[tri.a], z_[tri.a]};
            Point3D p1{x_[tri.b], y_[tri.b], z_[tri.b]};
            Point3D p2{x_[tri.c], y_[tri.c], z_[tri.c]};
            edgeSegments_.push_back(p0);
            edgeSegments_.push_back(p1);
            edgeSegments_.push_back(p1);
            edgeSegments_.push_back(p2);
            edgeSegments_.push_back(p2);
            edgeSegments_.push_back(p0);
        }
    }
}

void TrisurfPlot::prepare(render::Renderer& r) {
    projectSurface();


    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    if (!fillPositions_.empty()) {
        fillRenderer_->upload3D(std::span{fillPositions_},
                                std::span{fillColors_}, vp_);
    }

    if (config_.drawEdges && !edgeSegments_.empty()) {
        if (!edgeRenderer_) edgeRenderer_ = r.gpu().createLineSegmentRenderer();
        edgeRenderer_->upload3D(std::span{edgeSegments_},
                                config_.edgeColor, config_.edgeWidth, vp_);
    }

    prepared_ = true;
}

void TrisurfPlot::draw(render::Cmd& cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    if (!fillPositions_.empty())
        fillRenderer_->draw(cmd, vrect, t);

    if (config_.drawEdges && !edgeSegments_.empty())
        edgeRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(edgeSegments_.size()));
}

void TrisurfPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
        v.z.min = std::min(v.z.min, z_[i]);
        v.z.max = std::max(v.z.max, z_[i]);
    }
}

} // namespace volcano::plot
