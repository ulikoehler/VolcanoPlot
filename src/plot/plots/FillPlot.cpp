// volcano/plot/plots/FillPlot.cpp
#include "volcano/plot/plots/FillPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>

namespace volcano::plot {

namespace {

/// Tessellate a closed polygon as a triangle fan from its centroid.
/// This works for convex polygons. For concave polygons, a more
/// sophisticated tessellator (ear clipping) would be needed.
/// For typical fill() usage (area under a curve, simple shapes),
/// the fan is sufficient.
void buildFanTriangles(const std::vector<Point2D>& pts,
                       std::vector<Point2D>& outPos,
                       std::vector<Color>& outColor,
                       Color color) {
    if (pts.size() < 3) return;

    // Compute centroid.
    float cx = 0, cy = 0;
    for (const auto& p : pts) { cx += p.x; cy += p.y; }
    cx /= pts.size();
    cy /= pts.size();
    Point2D center{cx, cy};

    // Build triangle fan: (center, p[i], p[i+1]) for each edge.
    // The polygon is closed (last → first).
    size_t n = pts.size();
    outPos.reserve(outPos.size() + n * 3);
    outColor.reserve(outColor.size() + n * 3);
    for (size_t i = 0; i < n; ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % n];
        outPos.push_back(center);
        outPos.push_back(a);
        outPos.push_back(b);
        outColor.push_back(color);
        outColor.push_back(color);
        outColor.push_back(color);
    }
}

} // namespace

void FillPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());

    // Build triangle fan from the polygon points.
    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildFanTriangles(series_.points, positions, colors, series_.color);

    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{positions.data(), positions.size()},
                     std::span{colors.data(), colors.size()});
    prepared_ = true;
}

void FillPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void FillPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x);
        v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y);
        v.y.max = std::max(v.y.max, p.y);
    }
}

void FillPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                    renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
