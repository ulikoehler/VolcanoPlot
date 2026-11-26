// volcano/plot/plots/StemPlot.cpp — stem plot implementation
#include "volcano/plot/plots/StemPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/plot/Stroke.hpp"
#include "../VectorEmitHelpers.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <stdexcept>

namespace volcano::plot {

StemPlot::StemPlot(std::vector<float> x, std::vector<float> y,
                   StemConfig config)
    : x_(std::move(x)), y_(std::move(y)), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("StemPlot: x and y must have the same size");
}

StemPlot::StemPlot(std::vector<float> y, StemConfig config)
    : y_(std::move(y)), config_(std::move(config)) {
    x_.resize(y_.size());
    for (size_t i = 0; i < y_.size(); ++i) x_[i] = static_cast<float>(i);
}

void StemPlot::buildGeometry() {
    stemSegments_.clear();
    baselineSegments_.clear();
    markerPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    if (x_.empty()) return;

    // Build stem segments: vertical line from (x, baseline) to (x, y).
    for (size_t i = 0; i < x_.size(); ++i) {
        Point2D base{x_[i], config_.baseline};
        Point2D top{x_[i], y_[i]};
        stemSegments_.push_back(base);
        stemSegments_.push_back(top);

        if (config_.markers) {
            markerPoints_.push_back(top);
            markerColors_.push_back(config_.markerColor);
            markerSizes_.push_back(config_.markerSize);
        }
    }

    // Build baseline: horizontal line from (xMin, baseline) to (xMax, baseline).
    if (config_.showBaseline) {
        float xMin = *std::ranges::min_element(x_);
        float xMax = *std::ranges::max_element(x_);
        baselineSegments_.push_back({xMin, config_.baseline});
        baselineSegments_.push_back({xMax, config_.baseline});
    }
}

void StemPlot::prepare(render::Renderer& r) {
    buildGeometry();

    // mpl linewidths are in points — convert with the effective dpi.
    const float kPtPx = (owner() ? owner()->dpi() : 72.0f) / 72.0f;
    if (!stemRenderer_) stemRenderer_ = r.gpu().createLineSegmentRenderer();
    if (!stemSegments_.empty()) {
        stemRenderer_->upload(std::span{stemSegments_}, config_.lineColor,
                             config_.lineWidth * kPtPx);
    }

    // The baseline is drawn in pixel space in draw() (mpl axhline).

    if (config_.markers && !markerPoints_.empty()) {
        if (!markerRenderer_) markerRenderer_ = r.gpu().createPointRenderer();
        markerRenderer_->upload(std::span{markerPoints_}, std::span{markerColors_},
                                std::span{markerSizes_});
    }

    prepared_ = true;
}

void StemPlot::draw(render::Cmd& cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || x_.empty()) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    if (config_.showBaseline && !baselineSegments_.empty()) {
        // mpl draws the stem baseline as axhline — it spans the axes
        // xlim, not just the data x range. Draw in pixel space.
        float py = rect.y +
            (1.0f - axes.dataToFraction({0.0f, config_.baseline}).y) *
                float(rect.height);
        Point2D pts[] = {
            {float(rect.x) - 10.0f, py},
            {float(rect.x + rect.width) + 10.0f, py}
        };
        r.gpu().spine().drawLineStrip(cmd, vrect, r.gpu().extent(),
                                      std::span{pts, 2}, config_.baselineColor,
                                      config_.baselineWidth * axes.dpi() /
                                          72.0f);
    }

    stemRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(stemSegments_.size()));

    if (config_.markers && !markerPoints_.empty())
        markerRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(markerPoints_.size()));
}

namespace {

MarkerStyle stemMarker(const std::string& s) {
    if (s == "square") return MarkerStyle::Square;
    if (s == "diamond") return MarkerStyle::Diamond;
    if (s == "plus") return MarkerStyle::Plus;
    if (s == "x") return MarkerStyle::X;
    if (s == "star") return MarkerStyle::Star;
    if (s == "triangle") return MarkerStyle::Triangle;
    return MarkerStyle::Circle;
}

} // namespace

void StemPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (stemSegments_.empty() && !x_.empty()) buildGeometry();
    auto toPx = pxMapper(axes, rect);
    const float kPtPx = axes.dpi() / 72.0f;
    render::VectorCanvas::Pen pen;
    pen.color = config_.lineColor;
    pen.width = config_.lineWidth * kPtPx;
    for (size_t i = 0; i + 1 < stemSegments_.size(); i += 2) {
        Point2D seg[2] = {toPx(stemSegments_[i]), toPx(stemSegments_[i + 1])};
        c.polyline(seg, pen);
    }
    if (config_.showBaseline && baselineSegments_.size() >= 2) {
        // mpl baseline is an axhline: spans the axes xlim.
        render::VectorCanvas::Pen bp;
        bp.color = config_.baselineColor;
        bp.width = config_.baselineWidth * kPtPx;
        float py = toPx({0.0f, config_.baseline}).y;
        Point2D seg[2] = {{float(rect.x), py},
                          {float(rect.x + rect.width), py}};
        c.polyline(seg, bp);
    }
    if (config_.markers && !markerPoints_.empty()) {
        auto g = markerGeom(stemMarker(config_.markerStyle));
        emitMarkerAt(c, toPx, markerPoints_, g, config_.markerSize,
                     config_.markerColor);
    }
}

void StemPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    v.y.min = std::min(v.y.min, config_.baseline);
    v.y.max = std::max(v.y.max, config_.baseline);
    for (float yv : y_) {
        v.y.min = std::min(v.y.min, yv);
        v.y.max = std::max(v.y.max, yv);
    }
}

} // namespace volcano::plot
