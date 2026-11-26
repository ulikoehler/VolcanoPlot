// volcano/plot/plots/SpyPlot.cpp — sparsity pattern plot implementation
#include "volcano/plot/plots/SpyPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

SpyPlot::SpyPlot(std::vector<float> data, uint32_t nrows, uint32_t ncols,
                 SpyConfig config)
    : data_(std::move(data)), nrows_(nrows), ncols_(ncols),
      config_(std::move(config)) {
    if (data_.size() != nrows_ * ncols_)
        throw std::invalid_argument("SpyPlot: data size must be nrows * ncols");
    if (nrows_ == 0 || ncols_ == 0)
        throw std::invalid_argument("SpyPlot: matrix dimensions must be non-zero");
}

void SpyPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    // mpl spy (imshow style): cell (i,j) is centered on the data
    // coordinates (i, j) — extent [-0.5, ncols-0.5] x
    // [-0.5, nrows-0.5]. Row 0 lands at the top through the inverted
    // y limits that Axes::spy() installs (origin='upper').

    float ms = std::clamp(config_.markerSize, 0.0f, 1.0f);
    float inset = (1.0f - ms) * 0.5f;

    for (uint32_t j = 0; j < nrows_; ++j) {
        for (uint32_t i = 0; i < ncols_; ++i) {
            float val = data_[j * ncols_ + i];
            bool isNonZero = std::abs(val) > config_.precision;

            Color color = isNonZero ? config_.color : config_.zeroColor;
            if (color.a == 0.0f) continue;  // skip transparent cells

            float x0 = float(i) - 0.5f + inset;
            float x1 = float(i) + 0.5f - inset;
            float y0 = float(j) - 0.5f + inset;
            float y1 = float(j) + 0.5f - inset;

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

void SpyPlot::prepare(render::Renderer& r) {
    buildGeometry();
    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    if (!fillPositions_.empty()) {
        fillRenderer_->upload(std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void SpyPlot::draw(render::Cmd& cmd, render::Renderer& r,
                   const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    fillRenderer_->draw(cmd, vrect, t);
}

void SpyPlot::contributeToAutoscale(Viewport& v) const {
    // mpl imshow extent: half a cell past the outer index centers.
    v.x.min = std::min(v.x.min, -0.5f);
    v.x.max = std::max(v.x.max, static_cast<float>(ncols_) - 0.5f);
    v.y.min = std::min(v.y.min, -0.5f);
    v.y.max = std::max(v.y.max, static_cast<float>(nrows_) - 0.5f);
}

} // namespace volcano::plot
