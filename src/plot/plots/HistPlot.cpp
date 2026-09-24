// volcano/plot/plots/HistPlot.cpp
#include "volcano/plot/plots/HistPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "../VectorEmitHelpers.hpp"
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
    // Merge all samples for shared bin-edge computation.
    std::vector<float> all;
    size_t total = 0;
    for (const auto& d : datasets_) total += d.size();
    all.reserve(total);
    for (const auto& d : datasets_)
        all.insert(all.end(), d.begin(), d.end());

    if (all.empty()) {
        binEdges_ = {0.0f, 1.0f};
        heights_.assign(std::max<size_t>(datasets_.size(), 1), {0.0f});
        return;
    }

    // Sort samples for quantile computation.
    std::sort(all.begin(), all.end());

    // Determine data range.
    float dataMin, dataMax;
    if (cfg_.range && cfg_.range->valid()) {
        dataMin = cfg_.range->min;
        dataMax = cfg_.range->max;
    } else {
        dataMin = all.front();
        dataMax = all.back();
    }
    if (dataMax <= dataMin) dataMax = dataMin + 1.0f;

    // Determine bin edges (shared across datasets — required for
    // barstacked stacking, matching matplotlib).
    if (cfg_.bins == HistBinMethod::Edges && cfg_.binEdges.size() >= 2) {
        binEdges_ = cfg_.binEdges;
    } else {
        // Bin count from the largest dataset (matplotlib uses per-dataset
        // counts for "auto"; shared edges use the max count).
        size_t maxN = 0;
        for (const auto& d : datasets_) maxN = std::max(maxN, d.size());
        std::vector<float> largest = all; // for IQR use merged samples
        int nBins;
        switch (cfg_.bins) {
            case HistBinMethod::Sturges: nBins = sturgesBins(maxN); break;
            case HistBinMethod::FD:      nBins = fdBins(largest, dataMin, dataMax); break;
            case HistBinMethod::Rice:    nBins = riceBins(maxN); break;
            case HistBinMethod::Square:  nBins = squareBins(maxN); break;
            case HistBinMethod::Fixed:   nBins = cfg_.binCount; break;
            case HistBinMethod::Auto:
            default:
                nBins = std::max(sturgesBins(maxN),
                                 fdBins(largest, dataMin, dataMax));
                break;
        }
        nBins = std::max(1, nBins);
        float binWidth = (dataMax - dataMin) / nBins;
        binEdges_.resize(nBins + 1);
        for (int i = 0; i <= nBins; ++i)
            binEdges_[i] = dataMin + i * binWidth;
    }

    // Count samples per dataset in the shared bins.
    size_t nBins = binEdges_.size() - 1;
    heights_.assign(datasets_.size(), std::vector<float>(nBins, 0.0f));
    for (size_t d = 0; d < datasets_.size(); ++d) {
        for (float s : datasets_[d]) {
            if (s < binEdges_.front() || s > binEdges_.back()) continue;
            auto it = std::upper_bound(binEdges_.begin(), binEdges_.end(), s);
            size_t idx = static_cast<size_t>(it - binEdges_.begin()) - 1;
            if (idx >= nBins) idx = nBins - 1;  // last bin includes right edge
            heights_[d][idx] += 1.0f;
        }

        // Apply normalization (per dataset, like matplotlib).
        float n = static_cast<float>(datasets_[d].size());
        if (n <= 0) n = 1.0f;
        auto& h = heights_[d];
        switch (cfg_.norm) {
            case HistNorm::Density:
                for (size_t i = 0; i < nBins; ++i) {
                    float w = binEdges_[i + 1] - binEdges_[i];
                    if (w > 0) h[i] /= (n * w);
                }
                break;
            case HistNorm::Probability:
                for (auto& v : h) v /= n;
                break;
            case HistNorm::Cumulative: {
                float cum = 0;
                for (auto& v : h) { cum += v; v = cum; }
                break;
            }
            case HistNorm::Count:
            default:
                break;
        }
    }
    if (heights_.empty()) heights_.push_back(std::vector<float>(nBins, 0.0f));
}

namespace {

/// Emit a filled axis-aligned rectangle as two triangles.
void emitQuad(std::vector<Point2D>& pos, float x0, float y0,
              float x1, float y1) {
    pos.insert(pos.end(), {{x0, y0}, {x1, y0}, {x0, y1},
                           {x1, y0}, {x1, y1}, {x0, y1}});
}

} // namespace

void HistPlot::buildBarVertices(std::vector<Point2D>& positions,
                                std::vector<Color>& colors) const {
    size_t nBins = binEdges_.size() - 1;
    if (nBins == 0) return;
    size_t nSets = heights_.size();
    auto setColor = [&](size_t d) {
        return d < cfg_.colors.size() ? cfg_.colors[d] : cfg_.color;
    };

    if (cfg_.histtype == HistType::StepFilled) {
        // Filled step polygon per dataset down to the baseline.
        for (size_t d = 0; d < nSets; ++d) {
            Color c = setColor(d);
            for (size_t i = 0; i < nBins; ++i) {
                float e0 = binEdges_[i], e1 = binEdges_[i + 1];
                float h = heights_[d][i];
                if (cfg_.horizontal) emitQuad(positions, 0, e0, h, e1);
                else                 emitQuad(positions, e0, 0, e1, h);
                for (int k = 0; k < 6; ++k) colors.push_back(c);
            }
        }
        return;
    }

    if (cfg_.histtype == HistType::BarStacked) {
        // Stack datasets in each bin.
        for (size_t i = 0; i < nBins; ++i) {
            float base = 0.0f;
            for (size_t d = 0; d < nSets; ++d) {
                float h = heights_[d][i];
                if (cfg_.horizontal)
                    emitQuad(positions, base, binEdges_[i],
                             base + h, binEdges_[i + 1]);
                else
                    emitQuad(positions, binEdges_[i], base,
                             binEdges_[i + 1], base + h);
                base += h;
                Color c = setColor(d);
                for (int k = 0; k < 6; ++k) colors.push_back(c);
            }
        }
        return;
    }

    // HistType::Bar — side-by-side bars for multiple datasets.
    positions.reserve(nBins * 6 * nSets);
    colors.reserve(nBins * 6 * nSets);
    for (size_t i = 0; i < nBins; ++i) {
        float e0 = binEdges_[i], e1 = binEdges_[i + 1];
        float w = (e1 - e0) / float(std::max<size_t>(nSets, 1));
        for (size_t d = 0; d < nSets; ++d) {
            float h = heights_[d][i];
            if (cfg_.horizontal)
                emitQuad(positions, 0, e0 + w * float(d),
                         h, e0 + w * float(d + 1));
            else
                emitQuad(positions, e0 + w * float(d), 0,
                         e0 + w * float(d + 1), h);
            Color c = setColor(d);
            for (int k = 0; k < 6; ++k) colors.push_back(c);
        }
    }
}

void HistPlot::buildStepSegments() {
    // Unfilled step outlines (matplotlib histtype="step").
    stepSegs_.assign(heights_.size(), {});
    size_t nBins = binEdges_.size() - 1;
    for (size_t d = 0; d < heights_.size(); ++d) {
        auto& segs = stepSegs_[d];
        const auto& h = heights_[d];
        for (size_t i = 0; i < nBins; ++i) {
            float e0 = binEdges_[i], e1 = binEdges_[i + 1];
            // Horizontal top of the bin + vertical riser to the next bin.
            if (cfg_.horizontal) {
                segs.push_back({h[i], e0});
                segs.push_back({h[i], e1});
                if (i + 1 < nBins) {
                    segs.push_back({h[i], e1});
                    segs.push_back({h[i + 1], e1});
                }
            } else {
                segs.push_back({e0, h[i]});
                segs.push_back({e1, h[i]});
                if (i + 1 < nBins) {
                    segs.push_back({e1, h[i]});
                    segs.push_back({e1, h[i + 1]});
                }
            }
        }
    }
}

void HistPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                   r.backend().sampleCount(), r.pipelineCache());

    computeBins();

    if (cfg_.histtype == HistType::Step) {
        buildStepSegments();
        stepRenderers_.clear();
        stepCounts_.clear();
        for (size_t d = 0; d < stepSegs_.size(); ++d) {
            if (stepSegs_[d].empty()) continue;
            auto sr = std::make_unique<render::primitives::LineSegmentRenderer>();
            sr->init(ctx.device.handle(), r.backend().renderPass(),
                     r.backend().sampleCount(), r.pipelineCache());
            Color c = d < cfg_.colors.size() ? cfg_.colors[d] : cfg_.color;
            c.a = 1.0f; // step outlines are opaque (matplotlib)
            sr->upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                       ctx.graphicsPool.handle(), ctx.allocator.handle(),
                       std::span{stepSegs_[d]}, c, cfg_.stepLineWidth);
            stepCounts_.push_back(uint32_t(stepSegs_[d].size()));
            stepRenderers_.push_back(std::move(sr));
        }
    } else {
        std::vector<Point2D> positions;
        std::vector<Color> colors;
        buildBarVertices(positions, colors);
        if (!positions.empty())
            renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{positions}, std::span{colors});
    }

    // Store unique data points for GPU autoscale (corners of each bar).
    uploadedPoints_.clear();
    for (size_t i = 0; i < binEdges_.size(); ++i) {
        float h = 0.0f;
        for (const auto& set : heights_)
            if (i < set.size())
                h = (cfg_.histtype == HistType::BarStacked)
                        ? h + set[i]           // stacked top
                        : std::max(h, set[i]); // tallest bar
        if (cfg_.horizontal) {
            uploadedPoints_.push_back({0.0f, binEdges_[i]});
            uploadedPoints_.push_back({h, binEdges_[i]});
        } else {
            uploadedPoints_.push_back({binEdges_[i], 0.0f});
            uploadedPoints_.push_back({binEdges_[i], h});
        }
    }
    prepared_ = true;
}

void HistPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect = clipRectVk(rect, r.backend().extent());
    if (cfg_.histtype == HistType::Step) {
        for (size_t i = 0; i < stepRenderers_.size(); ++i)
            stepRenderers_[i]->draw(cmd, vrect, t, stepCounts_[i]);
    } else {
        renderer_.draw(cmd, vrect, t);
    }
}

void HistPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (binEdges_.empty()) computeBins();
    auto toPx = pxMapper(axes, rect);
    auto setColor = [&](size_t d) {
        return d < cfg_.colors.size() ? cfg_.colors[d] : cfg_.color;
    };
    if (cfg_.histtype == HistType::Step) {
        if (stepSegs_.empty()) buildStepSegments();
        for (size_t d = 0; d < stepSegs_.size(); ++d) {
            Color col = setColor(d);
            col.a = 1.0f;
            render::VectorCanvas::Pen pen;
            pen.color = col;
            pen.width = cfg_.stepLineWidth;
            for (size_t i = 0; i + 1 < stepSegs_[d].size(); i += 2) {
                Point2D seg[2] = {toPx(stepSegs_[d][i]),
                                  toPx(stepSegs_[d][i + 1])};
                c.polyline(seg, pen);
            }
        }
        return;
    }
    std::vector<Point2D> positions;
    std::vector<Color> colors;
    buildBarVertices(positions, colors);
    // Every 6 verts = one bar quad: {bl,br,ul},{br,ur,ul} → polygon
    // corners 0,1,4,2.
    for (size_t i = 0; i + 5 < positions.size(); i += 6) {
        Point2D q[4] = {toPx(positions[i]), toPx(positions[i + 1]),
                        toPx(positions[i + 4]), toPx(positions[i + 2])};
        c.polygon(q, colors[i]);
    }
}

void HistPlot::contributeToAutoscale(Viewport& v) const {
    if (binEdges_.empty() || heights_.empty() || heights_.front().empty()) {
        for (const auto& d : datasets_)
            for (float s : d) {
                v.x.min = std::min(v.x.min, s);
                v.x.max = std::max(v.x.max, s);
            }
        return;
    }

    float maxH = 0.0f;
    size_t nBins = heights_.front().size();
    for (size_t i = 0; i < nBins; ++i) {
        float h = 0.0f;
        for (const auto& set : heights_)
            if (i < set.size())
                h = (cfg_.histtype == HistType::BarStacked)
                        ? h + set[i] : std::max(h, set[i]);
        maxH = std::max(maxH, h);
    }
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
