// volcano/plot/plots/ErrorbarPlot.cpp
#include "volcano/plot/plots/ErrorbarPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/plot/Stroke.hpp"
#include "../VectorEmitHelpers.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>

namespace volcano::plot {

std::pair<float, float> ErrorbarPlot::xerrBounds(size_t i) const {
    float lower, upper;
    if (!cfg_.xerrLower.empty() && !cfg_.xerrUpper.empty()) {
        lower = cfg_.xerrLower[i];
        upper = cfg_.xerrUpper[i];
    } else if (!cfg_.xerr.empty()) {
        lower = cfg_.xerr[i];
        upper = cfg_.xerr[i];
    } else {
        return {x_[i], x_[i]};
    }
    return {x_[i] - lower, x_[i] + upper};
}

std::pair<float, float> ErrorbarPlot::yerrBounds(size_t i) const {
    float lower, upper;
    if (!cfg_.yerrLower.empty() && !cfg_.yerrUpper.empty()) {
        lower = cfg_.yerrLower[i];
        upper = cfg_.yerrUpper[i];
    } else if (!cfg_.yerr.empty()) {
        lower = cfg_.yerr[i];
        upper = cfg_.yerr[i];
    } else {
        return {y_[i], y_[i]};
    }
    return {y_[i] - lower, y_[i] + upper};
}

void ErrorbarPlot::buildErrorSegments() {
    errorSegments_.clear();
    size_t n = x_.size();
    if (n == 0) return;

    // Estimate cap size in data units. Convert pixel cap size to data units
    // using the viewport span. We use a rough estimate based on the data range.
    float xSpan = *std::max_element(x_.begin(), x_.end()) -
                  *std::min_element(x_.begin(), x_.end());
    float ySpan = *std::max_element(y_.begin(), y_.end()) -
                  *std::min_element(y_.begin(), y_.end());
    if (xSpan <= 0) xSpan = 1;
    if (ySpan <= 0) ySpan = 1;

    // Cap size as fraction of data span (approximate pixel→data conversion).
    // The actual pixel size depends on the axes rect, but we use a reasonable
    // approximation: capSize pixels / canvasWidth * dataSpan.
    // Since we don't know the canvas size here, we use a small fraction.
    float xCapData = cfg_.capSize * xSpan / 256.0f;
    float yCapData = cfg_.capSize * ySpan / 256.0f;

    bool hasXerr = !cfg_.xerr.empty() ||
                   (!cfg_.xerrLower.empty() && !cfg_.xerrUpper.empty());
    bool hasYerr = !cfg_.yerr.empty() ||
                   (!cfg_.yerrLower.empty() && !cfg_.yerrUpper.empty());

    auto flag = [](const std::vector<bool>& f, size_t i) {
        return i < f.size() ? f[i] : false;
    };
    uint32_t every = std::max(1u, cfg_.errorevery);

    for (size_t i = 0; i < n; ++i) {
        if (i % every != 0) continue;  // mpl errorevery

        // Vertical error bar (y direction).
        if (hasYerr) {
            auto [ylo, yhi] = yerrBounds(i);
            bool up = flag(cfg_.uplims, i);    // upper limit → bar goes down
            bool lo = flag(cfg_.lolims, i);    // lower limit → bar goes up
            if (up && !lo) yhi = y_[i];
            if (lo && !up) ylo = y_[i];
            errorSegments_.push_back({x_[i], ylo});
            errorSegments_.push_back({x_[i], yhi});

            if (cfg_.drawCaps || up || lo) {
                if (!lo) {
                    if (up) {  // arrowhead at the point end (limit)
                        errorSegments_.push_back({x_[i] - xCapData, yhi - yCapData});
                        errorSegments_.push_back({x_[i], yhi});
                        errorSegments_.push_back({x_[i] + xCapData, yhi - yCapData});
                        errorSegments_.push_back({x_[i], yhi});
                    } else {
                        errorSegments_.push_back({x_[i] - xCapData, yhi});
                        errorSegments_.push_back({x_[i] + xCapData, yhi});
                    }
                }
                if (!up) {
                    if (lo) {  // arrowhead at the point end
                        errorSegments_.push_back({x_[i] - xCapData, ylo + yCapData});
                        errorSegments_.push_back({x_[i], ylo});
                        errorSegments_.push_back({x_[i] + xCapData, ylo + yCapData});
                        errorSegments_.push_back({x_[i], ylo});
                    } else {
                        errorSegments_.push_back({x_[i] - xCapData, ylo});
                        errorSegments_.push_back({x_[i] + xCapData, ylo});
                    }
                }
            }
        }

        // Horizontal error bar (x direction).
        if (hasXerr) {
            auto [xlo, xhi] = xerrBounds(i);
            bool xu = flag(cfg_.xuplims, i);
            bool xl = flag(cfg_.xlolims, i);
            if (xu && !xl) xhi = x_[i];
            if (xl && !xu) xlo = x_[i];
            errorSegments_.push_back({xlo, y_[i]});
            errorSegments_.push_back({xhi, y_[i]});

            if (cfg_.drawCaps || xu || xl) {
                if (!xl) {
                    if (xu) {
                        errorSegments_.push_back({xhi - xCapData, y_[i] - yCapData});
                        errorSegments_.push_back({xhi, y_[i]});
                        errorSegments_.push_back({xhi - xCapData, y_[i] + yCapData});
                        errorSegments_.push_back({xhi, y_[i]});
                    } else {
                        errorSegments_.push_back({xhi, y_[i] - yCapData});
                        errorSegments_.push_back({xhi, y_[i] + yCapData});
                    }
                }
                if (!xu) {
                    if (xl) {
                        errorSegments_.push_back({xlo + xCapData, y_[i] - yCapData});
                        errorSegments_.push_back({xlo, y_[i]});
                        errorSegments_.push_back({xlo + xCapData, y_[i] + yCapData});
                        errorSegments_.push_back({xlo, y_[i]});
                    } else {
                        errorSegments_.push_back({xlo, y_[i] - yCapData});
                        errorSegments_.push_back({xlo, y_[i] + yCapData});
                    }
                }
            }
        }
    }

    errorVertexCount_ = static_cast<uint32_t>(errorSegments_.size());
    hasErrors_ = hasXerr || hasYerr;
}

void ErrorbarPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    auto device = ctx.device.handle();
    auto queue = ctx.device.graphicsQueue();
    auto pool = ctx.graphicsPool.handle();
    auto allocator = ctx.allocator.handle();
    auto renderPass = r.backend().renderPass();
    auto samples = r.backend().sampleCount();

    // Build error bar segments.
    buildErrorSegments();

    // Initialize connecting line renderer.
    if (cfg_.drawLine && x_.size() >= 2) {
        lineRenderer_.init(device, renderPass, samples, r.pipelineCache());
        std::vector<Point2D> linePoints;
        linePoints.reserve(x_.size());
        for (size_t i = 0; i < x_.size(); ++i)
            linePoints.push_back({x_[i], y_[i]});
        lineRenderer_.upload(device, queue, pool, allocator,
                             std::span{linePoints.data(), linePoints.size()},
                             cfg_.color, cfg_.lineWidth);
    }

    // Initialize error bar segment renderer.
    if (hasErrors_ && errorVertexCount_ >= 2) {
        errorRenderer_.init(device, renderPass, samples, r.pipelineCache());
        errorRenderer_.upload(device, queue, pool, allocator,
                              std::span{errorSegments_.data(),
                                        errorSegments_.size()},
                              cfg_.errorbarColor, cfg_.errorbarWidth);
    }

    // Initialize point renderer for markers.
    if (cfg_.drawMarker && !x_.empty()) {
        pointRenderer_.init(device, renderPass, samples,
                            r.descriptorPool(), r.pipelineCache());
        std::vector<Point2D> pts;
        std::vector<Color> colors;
        std::vector<float> sizes;
        pts.reserve(x_.size());
        colors.resize(x_.size(), cfg_.markerColor);
        sizes.resize(x_.size(), cfg_.markerSize);
        for (size_t i = 0; i < x_.size(); ++i)
            pts.push_back({x_[i], y_[i]});
        pointRenderer_.upload(device, queue, pool, allocator,
                              std::span{pts.data(), pts.size()},
                              std::span{colors.data(), colors.size()},
                              std::span{sizes.data(), sizes.size()});
    }

    prepared_ = true;
}

void ErrorbarPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    // Draw error bars first (behind line and markers).
    if (hasErrors_ && errorVertexCount_ >= 2)
        errorRenderer_.draw(cmd, vrect, t, errorVertexCount_);

    // Draw connecting line.
    if (cfg_.drawLine && x_.size() >= 2)
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(x_.size()));

    // Draw markers on top.
    if (cfg_.drawMarker && !x_.empty())
        pointRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(x_.size()));
}

void ErrorbarPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                              Rect2D rect) {
    if (errorSegments_.empty() && hasErrors_ == false)
        buildErrorSegments();
    auto toPx = pxMapper(axes, rect);
    // Error bars + caps (line-segment soup, pairs of points).
    render::VectorCanvas::Pen ep;
    ep.color = cfg_.errorbarColor;
    ep.width = cfg_.errorbarWidth;
    for (size_t i = 0; i + 1 < errorSegments_.size(); i += 2) {
        Point2D seg[2] = {toPx(errorSegments_[i]),
                          toPx(errorSegments_[i + 1])};
        c.polyline(seg, ep);
    }
    // Connecting line.
    if (cfg_.drawLine && x_.size() >= 2) {
        std::vector<Point2D> px;
        px.reserve(x_.size());
        for (size_t i = 0; i < x_.size(); ++i)
            px.push_back(toPx({x_[i], y_[i]}));
        render::VectorCanvas::Pen lp;
        lp.color = cfg_.color;
        lp.width = cfg_.lineWidth;
        c.polyline(px, lp);
    }
    // Markers.
    if (cfg_.drawMarker && !x_.empty()) {
        std::vector<Point2D> pts(x_.size());
        for (size_t i = 0; i < x_.size(); ++i) pts[i] = {x_[i], y_[i]};
        auto g = markerGeom(MarkerStyle::Circle);
        emitMarkerAt(c, toPx, pts, g, cfg_.markerSize, cfg_.markerColor);
    }
}

void ErrorbarPlot::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        // Include error bar extents.
        auto [xlo, xhi] = xerrBounds(i);
        auto [ylo, yhi] = yerrBounds(i);
        v.x.min = std::min(v.x.min, xlo);
        v.x.max = std::max(v.x.max, xhi);
        v.y.min = std::min(v.y.min, ylo);
        v.y.max = std::max(v.y.max, yhi);
    }
}

void ErrorbarPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    // Use the error segment buffer if available (it contains all the
    // extreme points including error bar ends). Fall back to the line
    // renderer's buffer, then to CPU.
    if (hasErrors_ && errorRenderer_.pointCount() > 0) {
        auto r = reducer.reduceMinMax2D(errorRenderer_.pointBuffer(),
                                        errorRenderer_.pointCount());
        if (r) {
            v.x.min = std::min(v.x.min, r->minX);
            v.x.max = std::max(v.x.max, r->maxX);
            v.y.min = std::min(v.y.min, r->minY);
            v.y.max = std::max(v.y.max, r->maxY);
            return;
        }
    }
    if (cfg_.drawLine && lineRenderer_.pointCount() > 0) {
        auto r = reducer.reduceMinMax2D(lineRenderer_.pointBuffer(),
                                        lineRenderer_.pointCount());
        if (r) {
            v.x.min = std::min(v.x.min, r->minX);
            v.x.max = std::max(v.x.max, r->maxX);
            v.y.min = std::min(v.y.min, r->minY);
            v.y.max = std::max(v.y.max, r->maxY);
            return;
        }
    }
    contributeToAutoscale(v);
}

} // namespace volcano::plot
