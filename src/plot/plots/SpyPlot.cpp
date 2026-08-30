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

    // Matrix is rendered with row 0 at the top.
    // Data coordinates: x = [0, ncols], y = [0, nrows].
    // Row j (0-indexed from top) maps to y = [nrows - j - 1, nrows - j].
    // Column i maps to x = [i, i+1].

    float ms = std::clamp(config_.markerSize, 0.0f, 1.0f);
    float inset = (1.0f - ms) * 0.5f;

    for (uint32_t j = 0; j < nrows_; ++j) {
        for (uint32_t i = 0; i < ncols_; ++i) {
            float val = data_[j * ncols_ + i];
            bool isNonZero = std::abs(val) > config_.precision;

            Color color = isNonZero ? config_.color : config_.zeroColor;
            if (color.a == 0.0f) continue;  // skip transparent cells

            float x0 = i + inset;
            float x1 = i + 1 - inset;
            // Row 0 at top: y = nrows - j - 1 (bottom) to nrows - j (top).
            float y0 = static_cast<float>(nrows_ - j - 1) + inset;
            float y1 = static_cast<float>(nrows_ - j) - inset;

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

void SpyPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void SpyPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, 0.0f);
    v.x.max = std::max(v.x.max, static_cast<float>(ncols_));
    v.y.min = std::min(v.y.min, 0.0f);
    v.y.max = std::max(v.y.max, static_cast<float>(nrows_));
}

} // namespace volcano::plot
