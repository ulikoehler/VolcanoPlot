// volcano/plot/plots/HistPlot.cpp
#include "volcano/plot/plots/HistPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace volcano::plot {

namespace {

/// Sturges' rule: ceil(log2(n) + 1)
int sturgesBins(size_t n) {
    return static_cast<int>(std::ceil(std::log2(static_cast<double>(n)) + 1.0));
}

/// Rice's rule: 2 * n^(1/3)
int riceBins(size_t n) {
    return static_cast<int>(std::ceil(2.0 * std::cbrt(static_cast<double>(n))));
}

/// Square root rule: sqrt(n)
int squareBins(size_t n) {
    return static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
}

/// Freedman-Diaconis rule: bin_width = 2 * IQR / n^(1/3)
/// Returns the number of bins. iqrMin/iqrMax define the data range.
int fdBins(const std::vector<float>& sorted, float dataMin, float dataMax) {
    size_t n = sorted.size();
    if (n < 2) return 1;
    // IQR: Q3 - Q1
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

} // namespace

void HistPlot::computeBins() {
    if (samples_.empty()) {
        binEdges_ = {0.0f, 1.0f};
        heights_ = {0.0f};
        return;
    }

    // Sort samples for quantile computation.
    std::vector<float> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    // Determine data range.
    float dataMin, dataMax;
    if (cfg_.range && cfg_.range->valid()) {
        dataMin = cfg_.range->min;
        dataMax = cfg_.range->max;
    } else {
        dataMin = sorted.front();
        dataMax = sorted.back();
    }
    if (dataMax <= dataMin) dataMax = dataMin + 1.0f;

    // Determine bin edges.
    if (cfg_.bins == HistBinMethod::Edges && cfg_.binEdges.size() >= 2) {
        binEdges_ = cfg_.binEdges;
    } else {
        int nBins;
        switch (cfg_.bins) {
            case HistBinMethod::Sturges: nBins = sturgesBins(sorted.size()); break;
            case HistBinMethod::FD:      nBins = fdBins(sorted, dataMin, dataMax); break;
            case HistBinMethod::Rice:    nBins = riceBins(sorted.size()); break;
            case HistBinMethod::Square:  nBins = squareBins(sorted.size()); break;
            case HistBinMethod::Fixed:   nBins = cfg_.binCount; break;
            case HistBinMethod::Auto:
            default:
                nBins = std::max(sturgesBins(sorted.size()),
                                 fdBins(sorted, dataMin, dataMax));
                break;
        }
        nBins = std::max(1, nBins);
        float binWidth = (dataMax - dataMin) / nBins;
        binEdges_.resize(nBins + 1);
        for (int i = 0; i <= nBins; ++i)
            binEdges_[i] = dataMin + i * binWidth;
    }

    // Count samples in each bin.
    size_t nBins = binEdges_.size() - 1;
    heights_.assign(nBins, 0.0f);
    for (float s : samples_) {
        if (s < binEdges_.front() || s > binEdges_.back()) continue;
        // Find bin index via binary search on edges.
        auto it = std::upper_bound(binEdges_.begin(), binEdges_.end(), s);
        size_t idx = static_cast<size_t>(it - binEdges_.begin()) - 1;
        if (idx >= nBins) idx = nBins - 1;  // last bin includes right edge
        heights_[idx] += 1.0f;
    }

    // Apply normalization.
    float total = static_cast<float>(samples_.size());
    if (total <= 0) total = 1.0f;

    switch (cfg_.norm) {
        case HistNorm::Density:
            for (size_t i = 0; i < nBins; ++i) {
                float w = binEdges_[i + 1] - binEdges_[i];
                if (w > 0) heights_[i] /= (total * w);
            }
            break;
        case HistNorm::Probability:
            for (size_t i = 0; i < nBins; ++i)
                heights_[i] /= total;
            break;
        case HistNorm::Cumulative: {
            float cum = 0;
            for (size_t i = 0; i < nBins; ++i) {
                cum += heights_[i];
                heights_[i] = cum;
            }
            break;
        }
        case HistNorm::Count:
        default:
            break;  // raw counts
    }
}

void HistPlot::buildBarVertices(std::vector<Point2D>& positions,
                                std::vector<Color>& colors) const {
    size_t nBins = binEdges_.size() - 1;
    if (nBins == 0) return;
    positions.reserve(nBins * 6);
    colors.reserve(nBins * 6);

    for (size_t i = 0; i < nBins; ++i) {
        float x0 = binEdges_[i];
        float x1 = binEdges_[i + 1];
        float h = heights_[i];

        if (cfg_.horizontal) {
            // Horizontal: bars along X, height is bar width along Y.
            float y0 = x0;
            float y1 = x1;
            float w = h;
            Point2D bl{0, y0}, br{0, y1}, tl{w, y0}, tr{w, y1};
            positions.insert(positions.end(), {bl, br, tl, br, tr, tl});
        } else {
            // Vertical: bars along Y, baseline at y=0.
            Point2D bl{x0, 0}, br{x1, 0}, tl{x0, h}, tr{x1, h};
            positions.insert(positions.end(), {bl, br, tl, br, tr, tl});
        }
        for (int j = 0; j < 6; ++j) colors.push_back(cfg_.color);
    }
}

void HistPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());

    computeBins();

    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildBarVertices(positions, colors);

    // Store unique data points for GPU autoscale (corners of each bar).
    uploadedPoints_.clear();
    uploadedPoints_.reserve(binEdges_.size() * 2);
    for (size_t i = 0; i < binEdges_.size(); ++i) {
        float h = (i < heights_.size()) ? heights_[i] : 0.0f;
        if (cfg_.horizontal) {
            uploadedPoints_.push_back({0.0f, binEdges_[i]});
            uploadedPoints_.push_back({h, binEdges_[i]});
        } else {
            uploadedPoints_.push_back({binEdges_[i], 0.0f});
            uploadedPoints_.push_back({binEdges_[i], h});
        }
    }

    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                     ctx.graphicsPool.handle(), ctx.allocator.handle(),
                     std::span{positions.data(), positions.size()},
                     std::span{colors.data(), colors.size()});
    prepared_ = true;
}

void HistPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void HistPlot::contributeToAutoscale(Viewport& v) const {
    if (binEdges_.empty() || heights_.empty()) {
        // Use sample range if bins not yet computed.
        for (float s : samples_) {
            v.x.min = std::min(v.x.min, s);
            v.x.max = std::max(v.x.max, s);
        }
        return;
    }

    float maxH = *std::max_element(heights_.begin(), heights_.end());
    if (cfg_.horizontal) {
        v.y.min = std::min(v.y.min, binEdges_.front());
        v.y.max = std::max(v.y.max, binEdges_.back());
        v.x.min = std::min(v.x.min, 0.0f);
        v.x.max = std::max(v.x.max, maxH);
    } else {
        v.x.min = std::min(v.x.min, binEdges_.front());
        v.x.max = std::max(v.x.max, binEdges_.back());
        v.y.min = std::min(v.y.min, 0.0f);
        v.y.max = std::max(v.y.max, maxH);
    }
}

void HistPlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    // Use the FillRenderer's point buffer (triangle vertices contain
    // the bar corners, so min/max over them equals the data bbox).
    auto r = reducer.reduceMinMax2D(renderer_.pointBuffer(),
                                    renderer_.pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
