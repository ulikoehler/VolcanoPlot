// volcano/plot/plots/HistPlot.cpp
#include "volcano/plot/plots/HistPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/Offload.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "../VectorEmitHelpers.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>

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

// GPU uniform-bin counting: workgroup-private bins in shared memory
// (≤1024 bins) merged via global atomics. Returns empty on failure.
} // namespace

void HistPlot::computeBins(render::Renderer* r) {
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

    // Data range via minmax — sorting all samples is O(n log n) and only
    // needed when an IQR-based bin rule is in play.
    float dataMin, dataMax;
    if (cfg_.range && cfg_.range->valid()) {
        dataMin = cfg_.range->min;
        dataMax = cfg_.range->max;
    } else {
        auto [mn, mx] = std::ranges::minmax_element(all);
        dataMin = *mn;
        dataMax = *mx;
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
        const bool needIqr = cfg_.bins == HistBinMethod::FD ||
                             cfg_.bins == HistBinMethod::Auto;
        std::vector<float> sorted;
        if (needIqr) {
            // Order statistics on the device (`stats` offload switch):
            // the bitonic sort (op 60) mailboxes the sorted buffer back
            // one frame later, so the nth_element path covers the first
            // frame and identical data serves the cached device result.
            if (r && render::OffloadConfig::allowGpu(
                         render::OffloadConfig::global().stats)) {
                if (auto g = r->gpu().sortFloats(all);
                    g && g->size() == all.size())
                    sorted = std::move(*g);
            }
            if (sorted.size() != all.size()) {
                sorted = all;  // for IQR use merged samples
                // fdBins reads only the Q1/Q3 order statistics — two
                // nth_element passes (O(n) expected) beat a full sort.
                size_t m = sorted.size();
                if (m >= 2) {
                    auto q1 = sorted.begin() + ptrdiff_t(m / 4);
                    auto q3 = sorted.begin() + ptrdiff_t((3 * m) / 4);
                    std::nth_element(sorted.begin(), q1, sorted.end());
                    std::nth_element(q1 + 1, q3, sorted.end());
                }
            }
        }
        int nBins;
        switch (cfg_.bins) {
            case HistBinMethod::Sturges: nBins = sturgesBins(maxN); break;
            case HistBinMethod::FD:      nBins = fdBins(sorted, dataMin, dataMax); break;
            case HistBinMethod::Rice:    nBins = riceBins(maxN); break;
            case HistBinMethod::Square:  nBins = squareBins(maxN); break;
            case HistBinMethod::Fixed:   nBins = cfg_.binCount; break;
            case HistBinMethod::Auto:
            default:
                nBins = std::max(sturgesBins(maxN),
                                 fdBins(sorted, dataMin, dataMax));
                break;
        }
        nBins = std::max(1, nBins);
        float binWidth = (dataMax - dataMin) / nBins;
        binEdges_.resize(nBins + 1);
        for (int i = 0; i <= nBins; ++i)
            binEdges_[i] = dataMin + i * binWidth;
    }

    // Count samples per dataset in the shared bins. Evenly spaced edges
    // (the common case) allow a direct O(1) index instead of a binary
    // search per sample.
    size_t nBins = binEdges_.size() - 1;
    const float e0 = binEdges_.front(), e1 = binEdges_.back();
    bool uniform = nBins >= 1 && e1 > e0;
    if (uniform && nBins > 1) {
        float w0 = binEdges_[1] - binEdges_[0];
        for (size_t i = 1; i < nBins && uniform; ++i)
            uniform = std::abs(binEdges_[i + 1] - binEdges_[i] - w0) <=
                      w0 * 1e-4f;
    }
    const float invW = uniform ? float(nBins) / (e1 - e0) : 0.0f;
    heights_.assign(datasets_.size(), std::vector<float>(nBins, 0.0f));
    for (size_t d = 0; d < datasets_.size(); ++d) {
        const auto& data = datasets_[d];
        // Uniform bins + huge sample count: count in parallel — each
        // worker accumulates a private bin vector, merged afterwards.
        // Float partials are exact: increments of 1 stay integral well
        // past any realistic chunk size.
        constexpr size_t kPar = 1'000'000;
        unsigned nt = std::min<unsigned>(std::thread::hardware_concurrency(),
                                         8u);
        bool done = false;
        // GPU binning is opt-in: uploading the samples to a host-visible
        // buffer costs more than the 8-thread CPU count on discrete GPUs
        // (measured: 118 ms GPU vs ~50 ms CPU at 10M). Unified-memory
        // setups — and any integrator who wants the CPU free — can turn
        // it on with VOLCANO_GPU_OFFLOAD=binning=gpu (or the legacy
        // VOLCANO_GPU_HIST=1).
        const bool gpuHistOn = render::OffloadConfig::allowGpu(
            render::OffloadConfig::global().binning);
        if (gpuHistOn && uniform && r && data.size() >= kPar) {
            if (auto counts = r->gpu().histBin(data, nBins, e0, invW);
                counts && !counts->empty()) {
                for (size_t i = 0; i < nBins; ++i)
                    heights_[d][i] += float((*counts)[i]);
                done = true;
            }
        }
        if (!done && uniform && data.size() >= kPar && nt > 1) {
            std::vector<std::vector<float>> parts(
                nt, std::vector<float>(nBins, 0.0f));
            std::vector<std::thread> workers;
            workers.reserve(nt);
            for (unsigned t = 0; t < nt; ++t) {
                size_t lo = data.size() * t / nt;
                size_t hi = data.size() * (t + 1) / nt;
                workers.emplace_back([&, lo, hi, t] {
                    auto& hp = parts[t];
                    for (size_t i = lo; i < hi; ++i) {
                        float s = data[i];
                        if (s < e0 || s > e1) continue;
                        size_t idx = static_cast<size_t>((s - e0) * invW);
                        if (idx >= nBins) idx = nBins - 1;
                        hp[idx] += 1.0f;
                    }
                });
            }
            for (auto& w : workers) w.join();
            for (auto& p : parts)
                for (size_t i = 0; i < nBins; ++i)
                    heights_[d][i] += p[i];
        } else {
            for (float s : data) {
                if (s < e0 || s > e1) continue;
                size_t idx;
                if (uniform) {
                    idx = static_cast<size_t>((s - e0) * invW);
                    if (idx >= nBins) idx = nBins - 1;  // right edge
                } else {
                    auto it = std::upper_bound(binEdges_.begin(),
                                               binEdges_.end(), s);
                    idx = static_cast<size_t>(it - binEdges_.begin()) - 1;
                    if (idx >= nBins) idx = nBins - 1;
                }
                heights_[d][idx] += 1.0f;
            }
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
    if (!renderer_) renderer_ = r.gpu().createFillRenderer();

    computeBins(&r);

    if (cfg_.histtype == HistType::Step) {
        buildStepSegments();
        stepRenderers_.clear();
        stepCounts_.clear();
        for (size_t d = 0; d < stepSegs_.size(); ++d) {
            if (stepSegs_[d].empty()) continue;
            auto sr = r.gpu().createLineSegmentRenderer();
            Color c = d < cfg_.colors.size() ? cfg_.colors[d] : cfg_.color;
            c.a = 1.0f; // step outlines are opaque (matplotlib)
            sr->upload(std::span{stepSegs_[d]}, c, cfg_.stepLineWidth);
            stepCounts_.push_back(uint32_t(stepSegs_[d].size()));
            stepRenderers_.push_back(std::move(sr));
        }
    } else {
        std::vector<Point2D> positions;
        std::vector<Color> colors;
        buildBarVertices(positions, colors);
        if (!positions.empty())
            renderer_->upload(std::span{positions}, std::span{colors});
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

void HistPlot::draw(render::Cmd& cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    Rect2D vrect = clipRectVk(rect, r.gpu().extent());
    if (cfg_.histtype == HistType::Step) {
        for (size_t i = 0; i < stepRenderers_.size(); ++i)
            stepRenderers_[i]->draw(cmd, vrect, t, stepCounts_[i]);
    } else {
        renderer_->draw(cmd, vrect, t);
    }
}

void HistPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    if (binEdges_.empty()) computeBins(nullptr);
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
    auto r = reducer.reduceMinMax2D(renderer_->pointBuffer(),
                                    renderer_->pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX);
    v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY);
    v.y.max = std::max(v.y.max, r->maxY);
}

} // namespace volcano::plot
