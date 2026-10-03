// volcano/plot/plots/PcolorfastPlot.cpp — fast pseudocolor plot implementation
#include "volcano/plot/plots/PcolorfastPlot.hpp"
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

PcolorfastPlot::PcolorfastPlot(std::vector<float> x, std::vector<float> y,
                               std::vector<float> C, uint32_t nCols, uint32_t nRows,
                               PcolorfastConfig config)
    : x_(std::move(x)), y_(std::move(y)), C_(std::move(C)),
      nCols_(nCols), nRows_(nRows), config_(std::move(config)) {
    if (x_.size() != nCols_ + 1)
        throw std::invalid_argument("PcolorfastPlot: x must have nCols+1 elements");
    if (y_.size() != nRows_ + 1)
        throw std::invalid_argument("PcolorfastPlot: y must have nRows+1 elements");
    if (C_.size() != nCols_ * nRows_)
        throw std::invalid_argument("PcolorfastPlot: C must have nCols*nRows elements");
}

PcolorfastPlot::PcolorfastPlot(std::vector<float> C, uint32_t nCols, uint32_t nRows,
                               Range xRange, Range yRange,
                               PcolorfastConfig config)
    : C_(std::move(C)), nCols_(nCols), nRows_(nRows), config_(std::move(config)) {
    if (C_.size() != nCols_ * nRows_)
        throw std::invalid_argument("PcolorfastPlot: C must have nCols*nRows elements");
    if (nCols_ == 0 || nRows_ == 0)
        throw std::invalid_argument("PcolorfastPlot: grid dimensions must be non-zero");

    // Generate uniform cell edges from the extent.
    float dx = xRange.span() / nCols_;
    float dy = yRange.span() / nRows_;
    x_.resize(nCols_ + 1);
    y_.resize(nRows_ + 1);
    for (uint32_t i = 0; i <= nCols_; ++i)
        x_[i] = xRange.min + i * dx;
    for (uint32_t j = 0; j <= nRows_; ++j)
        y_[j] = yRange.min + j * dy;
}

Color PcolorfastPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void PcolorfastPlot::computeValueRange() {
    if (config_.norm) {
        config_.norm->autoscale(C_);
        valueRange_ = {config_.norm->vmin(), config_.norm->vmax()};
        return;
    }
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

void PcolorfastPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    for (uint32_t j = 0; j < nRows_; ++j) {
        for (uint32_t i = 0; i < nCols_; ++i) {
            float val = C_[j * nCols_ + i];
            if (config_.skipNaN && std::isnan(val) && !cmap.bad) continue;

            float t;
            if (config_.norm) {
                t = (*config_.norm)(val);
            } else {
                t = (val - valueRange_.min) / vspan;
            }
            // NaN t -> cmap.bad (or transparent); out-of-range -> under/over.
            Color color = cmap.sample(t);

            float x0 = x_[i];
            float x1 = x_[i + 1];
            float y0 = y_[j];
            float y1 = y_[j + 1];

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

bool PcolorfastPlot::buildGeometryGpu(render::Renderer& r) {
    const uint64_t nVerts = uint64_t(nCols_) * nRows_ * 6;
    if (nCols_ == 0 || nRows_ == 0 || nVerts > (1ull << 31)) return false;

    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    // Per-cell normalized t on the CPU — one evaluation per value, so
    // arbitrary polymorphic norms stay supported (same as pcolormesh).
    std::vector<float> tvals(C_.size());
    for (size_t i = 0; i < C_.size(); ++i) {
        float v = C_[i];
        tvals[i] = std::isnan(v) ? std::numeric_limits<float>::quiet_NaN()
                   : config_.norm ? (*config_.norm)(v)
                                  : (v - valueRange_.min) / vspan;
    }

    // 259-entry LUT: [0..255] regular, 256 under, 257 over, 258 bad.
    std::vector<Color> lut(259);
    for (int i = 0; i < 256; ++i)
        lut[i] = cmap.sample(float(i) / 255.0f);
    lut[256] = cmap.under.value_or(cmap.sample(0.0f));
    lut[257] = cmap.over.value_or(cmap.sample(1.0f));
    lut[258] = cmap.bad.value_or(Color::transparent());

    const uint32_t flags =
        (cmap.bad ? 1u : 0u) | (config_.skipNaN ? 2u : 0u);
    render::GpuBuf posTok = 0, colTok = 0;
    if (!r.gpu().pcmTessellate(x_, y_, tvals, lut, nCols_, nRows_,
                               /*gouraud=*/false, flags, posTok, colTok))
        return false;
    fillRenderer_->adoptBuffers(posTok, colTok, uint32_t(nVerts));
    return true;
}

void PcolorfastPlot::prepare(render::Renderer& r) {
    computeValueRange();
    if (!fillRenderer_) fillRenderer_ = r.gpu().createFillRenderer();
    // The device tessellation emits the same 6-vert-per-cell soup the
    // CPU loop builds. Same policy as pcolormesh: opt-in for small
    // grids, automatic at ≥16384 cells.
    const uint64_t cells = uint64_t(nCols_) * nRows_;
    bool gpu = config_.gpuTessellate > 0 ||
               (config_.gpuTessellate < 0 && cells >= 16384);
    if (gpu) gpu = buildGeometryGpu(r);
    if (!gpu) {
        buildGeometry();
        if (!fillPositions_.empty()) {
            fillRenderer_->upload(std::span{fillPositions_},
                                  std::span{fillColors_});
        }
    }
    prepared_ = true;
}

void PcolorfastPlot::draw(render::Cmd& cmd, render::Renderer& r,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    if (!fillRenderer_ || fillRenderer_->pointCount() == 0) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    fillRenderer_->draw(cmd, vrect, t);
}

void PcolorfastPlot::contributeToAutoscale(Viewport& v) const {
    if (!x_.empty()) {
        v.x.min = std::min(v.x.min, x_.front());
        v.x.max = std::max(v.x.max, x_.back());
    }
    if (!y_.empty()) {
        v.y.min = std::min(v.y.min, y_.front());
        v.y.max = std::max(v.y.max, y_.back());
    }
}

} // namespace volcano::plot
