// volcano/plot/plots/TripcolorPlot.cpp — pseudocolor on triangular grids
#include "volcano/plot/plots/TripcolorPlot.hpp"
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

TripcolorPlot::TripcolorPlot(std::vector<float> x, std::vector<float> y,
                             std::vector<float> z, TripcolorConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("TripcolorPlot: x, y, z must have the same size");
    if (x_.size() < 3)
        throw std::invalid_argument("TripcolorPlot: need at least 3 points");
}

TripcolorPlot::TripcolorPlot(std::vector<float> x, std::vector<float> y,
                             std::vector<Triangle> triangles,
                             std::vector<float> facevalues,
                             TripcolorConfig config)
    : x_(std::move(x)), y_(std::move(y)),
      tris_(std::move(triangles)), facevalues_(std::move(facevalues)),
      useFacevalues_(true), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("TripcolorPlot: x and y must have the same size");
    if (facevalues_.size() != tris_.size())
        throw std::invalid_argument("TripcolorPlot: facevalues must have one per triangle");
}

Color TripcolorPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void TripcolorPlot::computeValueRange() {
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    if (useFacevalues_) {
        for (float v : facevalues_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    } else {
        for (float v : z_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
    if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
    valueRange_ = {vmin, vmax};
}

void TripcolorPlot::buildGeometry() {
    positions_.clear();
    colors_.clear();

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    auto sampleColor = [&](float v) {
        if (std::isnan(v)) return Color::transparent();
        float t = std::clamp((v - valueRange_.min) / vspan, 0.0f, 1.0f);
        return cmap.sample(t);
    };

    for (const auto& tri : tris_) {
        Point2D p0{x_[tri.a], y_[tri.a]};
        Point2D p1{x_[tri.b], y_[tri.b]};
        Point2D p2{x_[tri.c], y_[tri.c]};

        Color c0, c1, c2;
        if (useFacevalues_) {
            // One color per face.
            size_t idx = &tri - &tris_[0];
            Color fc = sampleColor(facevalues_[idx]);
            if (fc.a == 0.0f) continue;
            c0 = c1 = c2 = fc;
        } else if (config_.shading == TriShading::Flat) {
            // Average of 3 vertex values.
            float avg = (z_[tri.a] + z_[tri.b] + z_[tri.c]) / 3.0f;
            Color fc = sampleColor(avg);
            if (fc.a == 0.0f) continue;
            c0 = c1 = c2 = fc;
        } else {
            // Gouraud: per-vertex colors.
            c0 = sampleColor(z_[tri.a]);
            c1 = sampleColor(z_[tri.b]);
            c2 = sampleColor(z_[tri.c]);
            if (c0.a == 0.0f && c1.a == 0.0f && c2.a == 0.0f) continue;
        }

        positions_.push_back(p0);
        positions_.push_back(p1);
        positions_.push_back(p2);
        colors_.push_back(c0);
        colors_.push_back(c1);
        colors_.push_back(c2);
    }
}

void TripcolorPlot::prepare(render::Renderer& r) {
    // Triangulate if not provided.
    if (!useFacevalues_ && tris_.empty()) {
        std::vector<Point2D> pts(x_.size());
        for (size_t i = 0; i < x_.size(); ++i)
            pts[i] = {x_[i], y_[i]};
        tris_ = delaunay(pts);
    }

    computeValueRange();
    buildGeometry();

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

void TripcolorPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void TripcolorPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
    }
}

} // namespace volcano::plot
