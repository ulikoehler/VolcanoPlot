// volcano/plot/plots/PcolormeshPlot.cpp — pseudocolor mesh plot implementation
#include "volcano/plot/plots/PcolormeshPlot.hpp"
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

PcolormeshPlot::PcolormeshPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> C,
                               uint32_t nCols, uint32_t nRows,
                               PcolormeshConfig config)
    : x_(std::move(x)), y_(std::move(y)), C_(std::move(C)),
      nCols_(nCols), nRows_(nRows), config_(std::move(config)) {
    if (x_.size() != nCols_ + 1)
        throw std::invalid_argument("PcolormeshPlot: x must have nCols+1 elements");
    if (y_.size() != nRows_ + 1)
        throw std::invalid_argument("PcolormeshPlot: y must have nRows+1 elements");
    if (C_.size() != nCols_ * nRows_)
        throw std::invalid_argument("PcolormeshPlot: C must have nCols*nRows elements");
}

Color PcolormeshPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void PcolormeshPlot::computeValueRange() {
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
        return;
    }
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for (float v : C_) {
        if (std::isnan(v)) continue;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
    valueRange_ = {vmin, vmax};
}

void PcolormeshPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    for (uint32_t j = 0; j < nRows_; ++j) {
        for (uint32_t i = 0; i < nCols_; ++i) {
            float val = C_[j * nCols_ + i];
            if (config_.skipNaN && std::isnan(val)) continue;

            // Cell corners.
            float x0 = x_[i], x1 = x_[i + 1];
            float y0 = y_[j], y1 = y_[j + 1];

            // Color from colormap.
            float t = (val - valueRange_.min) / vspan;
            t = std::clamp(t, 0.0f, 1.0f);
            Color color = cmap.sample(t);

            // Two triangles per cell.
            Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
            // Triangle 1: bl, br, ul
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            // Triangle 2: br, ur, ul
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(color);
        }
    }
}

void PcolormeshPlot::prepare(render::Renderer& r) {
    computeValueRange();
    buildGeometry();
    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void PcolormeshPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void PcolormeshPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    for (float yv : y_) {
        v.y.min = std::min(v.y.min, yv);
        v.y.max = std::max(v.y.max, yv);
    }
}

} // namespace volcano::plot
