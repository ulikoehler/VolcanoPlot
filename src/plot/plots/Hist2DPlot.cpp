// volcano/plot/plots/Hist2DPlot.cpp — 2D histogram implementation
#include "volcano/plot/plots/Hist2DPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Sturges' rule: ceil(log2(n) + 1)
int sturgesBins(size_t n) {
    return static_cast<int>(std::ceil(std::log2(static_cast<double>(n)) + 1.0));
}

/// Freedman-Diaconis rule: bin_width = 2 * IQR / n^(1/3)
int fdBins(const std::vector<float>& sorted, float dataMin, float dataMax) {
    size_t n = sorted.size();
    if (n < 2) return 1;
    size_t q1Idx = n / 4;
    size_t q3Idx = (3 * n) / 4;
    float q1 = sorted[q1Idx];
    float q3 = sorted[q3Idx];
    float iqr = q3 - q1;
    if (iqr <= 0) return sturgesBins(n);
    double binWidth = 2.0 * iqr / std::cbrt(static_cast<double>(n));
    if (binWidth <= 0) return sturgesBins(n);
    return static_cast<int>(std::ceil((dataMax - dataMin) / binWidth));
}

/// Compute auto bin count for one axis.
int autoBinCount(const std::vector<float>& sorted, float dataMin, float dataMax) {
    return std::max(sturgesBins(sorted.size()),
                    fdBins(sorted, dataMin, dataMax));
}

/// Compute evenly-spaced bin edges.
std::vector<float> evenEdges(float lo, float hi, int nBins) {
    if (nBins < 1) nBins = 1;
    if (hi <= lo) hi = lo + 1.0f;
    float w = (hi - lo) / nBins;
    std::vector<float> edges(nBins + 1);
    for (int i = 0; i <= nBins; ++i)
        edges[i] = lo + i * w;
    return edges;
}

const Colormap& defaultColormap() {
    return colormaps::viridis();
}

} // namespace

Hist2DPlot::Hist2DPlot(std::vector<float> x, std::vector<float> y,
                       Hist2DConfig config)
    : x_(std::move(x)), y_(std::move(y)), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("Hist2DPlot: x and y must have the same size");
}

Color Hist2DPlot::legendColor() const {
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    return cmap.sample(0.5f);
}

void Hist2DPlot::computeBins() {
    if (x_.empty()) {
        xEdges_ = {0.0f, 1.0f};
        yEdges_ = {0.0f, 1.0f};
        counts_ = {0.0f};
        nBinsX_ = 1;
        nBinsY_ = 1;
        return;
    }

    // Sort copies for quantile computation.
    std::vector<float> sx = x_, sy = y_;
    std::sort(sx.begin(), sx.end());
    std::sort(sy.begin(), sy.end());

    // Determine data ranges.
    float xMin, xMax, yMin, yMax;
    if (config_.xRange && config_.xRange->valid()) {
        xMin = config_.xRange->min;
        xMax = config_.xRange->max;
    } else {
        xMin = sx.front();
        xMax = sx.back();
    }
    if (config_.yRange && config_.yRange->valid()) {
        yMin = config_.yRange->min;
        yMax = config_.yRange->max;
    } else {
        yMin = sy.front();
        yMax = sy.back();
    }
    if (xMax <= xMin) xMax = xMin + 1.0f;
    if (yMax <= yMin) yMax = yMin + 1.0f;

    // Compute bin edges.
    if (config_.bins == Hist2DBinMethod::Edges &&
        config_.xEdges.size() >= 2 && config_.yEdges.size() >= 2) {
        xEdges_ = config_.xEdges;
        yEdges_ = config_.yEdges;
    } else if (config_.bins == Hist2DBinMethod::Fixed) {
        xEdges_ = evenEdges(xMin, xMax, config_.nBinsX);
        yEdges_ = evenEdges(yMin, yMax, config_.nBinsY);
    } else {
        // Auto: compute per-axis.
        int nx = autoBinCount(sx, xMin, xMax);
        int ny = autoBinCount(sy, yMin, yMax);
        xEdges_ = evenEdges(xMin, xMax, nx);
        yEdges_ = evenEdges(yMin, yMax, ny);
    }

    nBinsX_ = static_cast<uint32_t>(xEdges_.size() - 1);
    nBinsY_ = static_cast<uint32_t>(yEdges_.size() - 1);

    // Count samples in 2D bins.
    counts_.assign(nBinsX_ * nBinsY_, 0.0f);
    for (size_t k = 0; k < x_.size(); ++k) {
        float xv = x_[k], yv = y_[k];
        if (xv < xEdges_.front() || xv > xEdges_.back()) continue;
        if (yv < yEdges_.front() || yv > yEdges_.back()) continue;
        // Find bin indices via binary search.
        auto itx = std::upper_bound(xEdges_.begin(), xEdges_.end(), xv);
        auto ity = std::upper_bound(yEdges_.begin(), yEdges_.end(), yv);
        uint32_t bx = static_cast<uint32_t>(itx - xEdges_.begin() - 1);
        uint32_t by = static_cast<uint32_t>(ity - yEdges_.begin() - 1);
        if (bx >= nBinsX_) bx = nBinsX_ - 1;
        if (by >= nBinsY_) by = nBinsY_ - 1;
        counts_[by * nBinsX_ + bx] += 1.0f;
    }

    // Apply normalization.
    if (config_.norm == Hist2DNorm::Density) {
        float total = static_cast<float>(x_.size());
        if (total > 0) {
            for (uint32_t j = 0; j < nBinsY_; ++j) {
                float binArea = (yEdges_[j + 1] - yEdges_[j]);
                for (uint32_t i = 0; i < nBinsX_; ++i) {
                    binArea *= (xEdges_[i + 1] - xEdges_[i]);
                    counts_[j * nBinsX_ + i] /= (total * binArea);
                }
            }
        }
    } else if (config_.norm == Hist2DNorm::Probability) {
        float total = static_cast<float>(x_.size());
        if (total > 0) {
            for (auto& c : counts_) c /= total;
        }
    }

    // Compute value range for color mapping.
    if (config_.valueRange.valid()) {
        valueRange_ = config_.valueRange;
    } else {
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (float c : counts_) {
            vmin = std::min(vmin, c);
            vmax = std::max(vmax, c);
        }
        if (vmin > vmax) { vmin = 0.0f; vmax = 1.0f; }
        valueRange_ = {vmin, vmax};
    }
}

void Hist2DPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();
    const Colormap& cmap = config_.cmap ? *config_.cmap : defaultColormap();
    float vspan = valueRange_.span();
    if (vspan <= 0.0f) vspan = 1.0f;

    for (uint32_t j = 0; j < nBinsY_; ++j) {
        for (uint32_t i = 0; i < nBinsX_; ++i) {
            float count = counts_[j * nBinsX_ + i];
            if (count <= 0.0f) continue;  // skip empty bins

            float x0 = xEdges_[i], x1 = xEdges_[i + 1];
            float y0 = yEdges_[j], y1 = yEdges_[j + 1];

            float t = (count - valueRange_.min) / vspan;
            t = std::clamp(t, 0.0f, 1.0f);
            Color color = cmap.sample(t);

            Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ul);
            fillPositions_.push_back(br);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ul);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(color);
        }
    }
}

void Hist2DPlot::prepare(render::Renderer& r) {
    computeBins();
    buildGeometry();
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void Hist2DPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}

void Hist2DPlot::contributeToAutoscale(Viewport& v) const {
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
