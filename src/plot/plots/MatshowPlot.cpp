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
    zorder = 0.0f;  // mpl AxesImage default zorder
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

    // mpl matshow (imshow origin='upper'): cell (i,j) centered on data
    // coords (i, j) — extent [-0.5, ncols-0.5] x [-0.5, nrows-0.5].
    // Row 0 lands at the top through the inverted y limits that
    // Axes::matshow() installs.
    for (uint32_t j = 0; j < nrows_; ++j) {
        for (uint32_t i = 0; i < ncols_; ++i) {
            float val = data_[j * ncols_ + i];
            float t;
            if (config_.norm) {
                t = (*config_.norm)(val);
            } else {
                t = (val - valueRange_.min) / vspan;
            }
            // NaN t -> cmap.bad (or transparent); out-of-range -> under/over.
            Color color = cmap.sample(t);
            if (color.a == 0.0f) continue;

            float x0 = float(i) - 0.5f;
            float x1 = float(i) + 0.5f;
            float y0 = float(j) - 0.5f;
            float y1 = float(j) + 0.5f;

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
    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    if (!fillPositions_.empty()) {
        fillRenderer_->upload(std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void MatshowPlot::draw(render::Cmd& cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    fillRenderer_->draw(cmd, vrect, t);
}

void MatshowPlot::contributeToAutoscale(Viewport& v) const {
    // mpl imshow extent: half a cell past the outer index centers.
    v.x.min = std::min(v.x.min, -0.5f);
    v.x.max = std::max(v.x.max, static_cast<float>(ncols_) - 0.5f);
    v.y.min = std::min(v.y.min, -0.5f);
    v.y.max = std::max(v.y.max, static_cast<float>(nrows_) - 0.5f);
}

} // namespace volcano::plot
