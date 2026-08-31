// volcano/plot/plots/FillPlot.cpp
#include "volcano/plot/plots/FillPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>

namespace volcano::plot {

namespace {

/// Build triangles for a filled polygon. For curves (x-monotonic point
/// sequences), this creates a triangle strip between the curve and the
/// baseline (y=0), which correctly handles concave shapes like damped
/// sine waves without the artifacts a triangle fan would produce.
/// For non-curve polygons, falls back to a triangle fan from the centroid.
void buildFillTriangles(const std::vector<Point2D>& pts,
                        std::vector<Point2D>& outPos,
                        std::vector<Color>& outColor,
                        Color color) {
    if (pts.size() < 3) return;

    // Check if the points form an x-monotonic curve (suitable for
    // baseline fill). This is the common case for fill(x, y).
    bool isCurve = true;
    bool xIncreasing = pts[1].x >= pts[0].x;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (xIncreasing ? pts[i].x < pts[i-1].x : pts[i].x > pts[i-1].x) {
            isCurve = false;
            break;
        }
    }

    if (isCurve) {
        // Build triangles between the curve and the closure line
        // (from first to last point). This matches matplotlib's fill()
        // which fills the polygon formed by the points, closed last→first.
        // For x-monotonic curves, we decompose into vertical strips.
        size_t n = pts.size();
        float x0 = pts.front().x, xN = pts.back().x;
        float y0 = pts.front().y, yN = pts.back().y;
        float xRange = xN - x0;
        if (std::abs(xRange) < 1e-12f) xRange = 1e-12f;
        // Closure line: y = y0 + (yN - y0) * (x - x0) / xRange
        auto closureY = [&](float x) { return y0 + (yN - y0) * (x - x0) / xRange; };

        outPos.reserve(outPos.size() + (n - 1) * 6);
        outColor.reserve(outColor.size() + (n - 1) * 6);
        for (size_t i = 0; i + 1 < n; ++i) {
            float cl = closureY(pts[i].x);     // closure left
            float cr = closureY(pts[i+1].x);   // closure right
            Point2D bl{pts[i].x,   cl};
            Point2D tl{pts[i].x,   pts[i].y};
            Point2D br{pts[i+1].x, cr};
            Point2D tr{pts[i+1].x, pts[i+1].y};
            // Triangle 1: bl, tl, tr
            outPos.push_back(bl); outPos.push_back(tl); outPos.push_back(tr);
            // Triangle 2: bl, tr, br
            outPos.push_back(bl); outPos.push_back(tr); outPos.push_back(br);
            for (int j = 0; j < 6; ++j) outColor.push_back(color);
        }
        return;
    }

    // Fallback: triangle fan from centroid (for non-curve polygons).
    float cx = 0, cy = 0;
    for (const auto& p : pts) { cx += p.x; cy += p.y; }
    cx /= pts.size();
    cy /= pts.size();
    Point2D center{cx, cy};

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

    // Build fill triangles from the polygon points.
    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildFillTriangles(series_.points, positions, colors, series_.color);

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
