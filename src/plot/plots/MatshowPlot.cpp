// volcano/plot/plots/MatshowPlot.cpp — matrix display implementation
#include "volcano/plot/plots/MatshowPlot.hpp"
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

MatshowPlot::MatshowPlot(std::vector<float> data, uint32_t nrows, uint32_t ncols,
                         MatshowConfig config)
    : data_(std::move(data)), nrows_(nrows), ncols_(ncols),
      config_(std::move(config)) {
    if (data_.size() != nrows_ * ncols_)
        throw std::invalid_argument("MatshowPlot: data size must be nrows * ncols");
    if (nrows_ == 0 || ncols_ == 0)
        throw std::invalid_argument("MatshowPlot: matrix dimensions must be non-zero");
}

Color MatshowPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void MatshowPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    // Compute value range.
    if (config_.norm) {
        config_.norm->autoscale(data_);
        valueRange_ = {config_.norm->vmin(), config_.norm->vmax()};
    } else if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
    } else {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float v : data_) {
            if (std::isnan(v)) continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
        valueRange_ = {vmin, vmax};
    }

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Matrix convention: row 0 at top.
    // Row j (0-indexed from top) maps to y = [nrows - j - 1, nrows - j].
    // Column i maps to x = [i, i+1].
    for (uint32_t j = 0; j < nrows_; ++j) {
        for (uint32_t i = 0; i < ncols_; ++i) {
            float val = data_[j * ncols_ + i];
            Color color;
            if (std::isnan(val)) {
                color = Color::transparent();
            } else {
                float t;
                if (config_.norm) {
                    t = (*config_.norm)(val);
                } else {
                    t = (val - valueRange_.min) / vspan;
                    t = std::clamp(t, 0.0f, 1.0f);
                }
                color = cmap.sample(t);
            }
            if (color.a == 0.0f) continue;

            float x0 = static_cast<float>(i);
            float x1 = static_cast<float>(i + 1);
            float y0 = static_cast<float>(nrows_ - j - 1);
            float y1 = static_cast<float>(nrows_ - j);

            Point2D bl{x0, y0}, br{x1, y0}, tl{x0, y1}, tr{x1, y1};
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tr);
            fillPositions_.push_back(tl);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(color);
        }
    }
}

void MatshowPlot::prepare(render::Renderer& r) {
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

void MatshowPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void MatshowPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, 0.0f);
    v.x.max = std::max(v.x.max, static_cast<float>(ncols_));
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, static_cast<float>(nrows_));
}

} // namespace volcano::plot
