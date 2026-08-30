// volcano/plot/plots/FillBetweenPlot.cpp
#include "volcano/plot/plots/FillBetweenPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>

namespace volcano::plot {

namespace {

/// Build a triangle strip between an upper curve (y1) and a lower curve (y2).
/// The strip is converted to a triangle list (3 vertices per triangle).
/// Upper curve goes left→right, lower curve goes right→left, forming a
/// closed band. We use a degenerate triangle strip approach:
///
///   For each segment i: (x[i], y1[i]) → (x[i+1], y1[i+1]) → (x[i], y2[i])
///   and (x[i+1], y1[i+1]) → (x[i+1], y2[i+1]) → (x[i], y2[i])
///
/// This produces 2 triangles per segment, 6 vertices per segment.
void buildFillBetweenTriangles(const std::vector<float>& x,
                               const std::vector<float>& y1,
                               const std::vector<float>& y2,
                               std::vector<Point2D>& outPos,
                               std::vector<Color>& outColor,
                               Color color) {
    size_t n = x.size();
    if (n < 2) return;
    size_t segCount = n - 1;
    outPos.reserve(outPos.size() + segCount * 6);
    outColor.reserve(outColor.size() + segCount * 6);

    for (size_t i = 0; i < segCount; ++i) {
        Point2D ul{x[i],   y1[i]};    // upper left
        Point2D ur{x[i+1], y1[i+1]};  // upper right
        Point2D ll{x[i],   y2[i]};    // lower left
        Point2D lr{x[i+1], y2[i+1]};  // lower right

        // Triangle 1: ul, ur, ll
        outPos.push_back(ul);
        outPos.push_back(ur);
        outPos.push_back(ll);
        // Triangle 2: ur, lr, ll
        outPos.push_back(ur);
        outPos.push_back(lr);
        outPos.push_back(ll);

        for (int j = 0; j < 6; ++j) outColor.push_back(color);
    }
}

} // namespace

void FillBetweenPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());

    // Build triangle list for the fill between area.
    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildFillBetweenTriangles(x_, y1_, y2_, positions, colors, color_);

    // Store the unique data points for GPU autoscale (the triangle vertices
    // include duplicates, so we build a separate list of unique points).
    uploadedPoints_.clear();
    uploadedPoints_.reserve(x_.size() * 2);  // upper + lower
    for (size_t i = 0; i < x_.size(); ++i) {
        uploadedPoints_.push_back({x_[i], y1_[i]});
        uploadedPoints_.push_back({x_[i], y2_[i]});
    }

    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{positions.data(), positions.size()},
                     std::span{colors.data(), colors.size()});
    prepared_ = true;
}

void FillBetweenPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                           const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void FillBetweenPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y1_[i]);
        v.y.max = std::max(v.y.max, y1_[i]);
        v.y.min = std::min(v.y.min, y2_[i]);
        v.y.max = std::max(v.y.max, y2_[i]);
    }
}

void FillBetweenPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    // Use the FillRenderer's point buffer (which contains triangle vertices,
    // but min/max over those is the same as min/max over the data points).
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                    renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
