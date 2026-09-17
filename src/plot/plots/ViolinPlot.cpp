// volcano/plot/plots/ViolinPlot.cpp — violin plot implementation
#include "volcano/plot/plots/ViolinPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {

/// Scott's rule for KDE bandwidth (matplotlib's default).
/// h = n^(-1/(d+4)) * sigma, where d=1 for 1D data.
float scottBandwidth(const std::vector<float>& data) {
    if (data.size() < 2) return 1.0f;
    float mean = 0.0f;
    for (float v : data) mean += v;
    mean /= data.size();
    float var = 0.0f;
    for (float v : data) { float d = v - mean; var += d * d; }
    var /= (data.size() - 1);
    float sigma = std::sqrt(var);
    return std::pow(static_cast<float>(data.size()), -0.2f) * sigma;
}

/// Gaussian kernel.
float gaussianKernel(float x) {
    return std::exp(-0.5f * x * x) * 0.39894228f;  // 1/sqrt(2*pi)
}

} // namespace

std::pair<std::vector<float>, std::vector<float>>
ViolinPlot::computeKde(const std::vector<float>& data) const {
    if (data.empty()) return {};
    float bw = cfg_.bandwidth > 0.0f ? cfg_.bandwidth : scottBandwidth(data);
    if (bw <= 0.0f) bw = 1.0f;

    // Evaluation range: data range ± 3*bw.
    float dmin = *std::min_element(data.begin(), data.end());
    float dmax = *std::max_element(data.begin(), data.end());
    float lo = dmin - 3.0f * bw;
    float hi = dmax + 3.0f * bw;
    uint32_t n = cfg_.numPoints;
    if (n < 2) n = 2;

    std::vector<float> yEval(n), density(n);
    for (uint32_t i = 0; i < n; ++i) {
        float y = lo + (hi - lo) * i / (n - 1);
        yEval[i] = y;
        float sum = 0.0f;
        for (float d : data)
            sum += gaussianKernel((y - d) / bw);
        density[i] = sum / (data.size() * bw);
    }
    return {yEval, density};
}

ViolinPlot::Stats ViolinPlot::computeStats(const std::vector<float>& data) const {
    std::vector<float> sorted = data;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](float p) -> float {
        if (sorted.empty()) return 0.0f;
        float idx = p / 100.0f * (sorted.size() - 1);
        size_t lo = static_cast<size_t>(idx);
        size_t hi = std::min(lo + 1, sorted.size() - 1);
        float frac = idx - lo;
        return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
    };
    return {pct(25.0f), pct(50.0f), pct(75.0f),
            sorted.front(), sorted.back()};
}

void ViolinPlot::buildGeometry() {
    bodyFillPos_.clear();
    bodyFillColors_.clear();
    bodyEdgeSegs_.clear();
    innerSegs_.clear();

    for (size_t g = 0; g < groups_.size(); ++g) {
        // matplotlib positions/widths per group.
        float centerX = g < cfg_.positions.size() ? cfg_.positions[g]
                                                : static_cast<float>(g + 1);
        float halfW = (g < cfg_.widths.size() ? cfg_.widths[g]
                                             : cfg_.width) * 0.5f;
        Color bodyColor = g < cfg_.bodyColors.size() ? cfg_.bodyColors[g]
                                                     : cfg_.bodyColor;
        // vert=false → transpose every emitted vertex (density along x).
        auto P = [&](float c, float v) -> Point2D {
            return cfg_.vert ? Point2D{c, v} : Point2D{v, c};
        };

        auto [yEval, density] = computeKde(groups_[g]);
        if (yEval.size() < 2) continue;

        // Normalize density to max=1, then scale by halfW.
        float maxD = *std::max_element(density.begin(), density.end());
        if (maxD <= 0.0f) maxD = 1.0f;

        // Build violin body as left/right contour points.
        size_t n = yEval.size();
        std::vector<Point2D> rightSide(n), leftSide(n);
        for (size_t i = 0; i < n; ++i) {
            float w = halfW * density[i] / maxD;
            rightSide[i] = P(centerX + w, yEval[i]);
            leftSide[i]  = P(centerX - w, yEval[i]);
        }

        // Strip-triangulate: for each pair of adjacent y values, create
        // a quad (two triangles) connecting right[i]→right[i+1]→left[i+1]→left[i].
        // This avoids the crossing artifacts that fan triangulation produces.
        for (size_t i = 0; i + 1 < n; ++i) {
            bodyFillPos_.push_back(rightSide[i]);
            bodyFillPos_.push_back(rightSide[i + 1]);
            bodyFillPos_.push_back(leftSide[i + 1]);
            bodyFillPos_.push_back(rightSide[i]);
            bodyFillPos_.push_back(leftSide[i + 1]);
            bodyFillPos_.push_back(leftSide[i]);
            for (int j = 0; j < 6; ++j) bodyFillColors_.push_back(bodyColor);
        }

        // Edge as line segments: right side top→bottom, bottom across,
        // left side bottom→top, top across (closed loop).
        for (size_t i = 0; i + 1 < n; ++i) {
            bodyEdgeSegs_.push_back(rightSide[i]);
            bodyEdgeSegs_.push_back(rightSide[i + 1]);
        }
        // Bottom cap: right[n-1] → left[n-1]
        bodyEdgeSegs_.push_back(rightSide[n - 1]);
        bodyEdgeSegs_.push_back(leftSide[n - 1]);
        for (size_t i = n - 1; i > 0; --i) {
            bodyEdgeSegs_.push_back(leftSide[i]);
            bodyEdgeSegs_.push_back(leftSide[i - 1]);
        }
        // Top cap: left[0] → right[0]
        bodyEdgeSegs_.push_back(leftSide[0]);
        bodyEdgeSegs_.push_back(rightSide[0]);

        // Inner elements: match matplotlib's violinplot defaults.
        //   showextrema=True: vertical whisker bar (min→max) + horizontal caps
        //   showmean=True:    horizontal line at the mean
        //   showbox=False:    no IQR box (off by default)
        auto st = computeStats(groups_[g]);
        float mean = 0.0f;
        for (float v : groups_[g]) mean += v;
        mean /= static_cast<float>(groups_[g].size());

        float capHalfW = halfW * 0.5f;  // matplotlib: cap half-width = width * 0.25

        if (cfg_.showExtrema) {
            // Vertical whisker bar from min to max.
            innerSegs_.push_back(P(centerX, st.min));
            innerSegs_.push_back(P(centerX, st.max));
            // Horizontal caps at min and max.
            innerSegs_.push_back(P(centerX - capHalfW, st.min));
            innerSegs_.push_back(P(centerX + capHalfW, st.min));
            innerSegs_.push_back(P(centerX - capHalfW, st.max));
            innerSegs_.push_back(P(centerX + capHalfW, st.max));
        }

        if (cfg_.showMean) {
            // Horizontal mean line (same width as caps in matplotlib).
            float meanHalfW = halfW * 0.5f;
            innerSegs_.push_back(P(centerX - meanHalfW, mean));
            innerSegs_.push_back(P(centerX + meanHalfW, mean));
        }

        if (cfg_.showBox) {
            // Optional IQR box + median (matplotlib: off by default).
            float boxHalfW = halfW * 0.1f;
            innerSegs_.push_back(P(centerX - boxHalfW, st.q1));
            innerSegs_.push_back(P(centerX - boxHalfW, st.q3));
            innerSegs_.push_back(P(centerX + boxHalfW, st.q1));
            innerSegs_.push_back(P(centerX + boxHalfW, st.q3));
            innerSegs_.push_back(P(centerX - boxHalfW, st.q1));
            innerSegs_.push_back(P(centerX + boxHalfW, st.q1));
            innerSegs_.push_back(P(centerX - boxHalfW, st.q3));
            innerSegs_.push_back(P(centerX + boxHalfW, st.q3));
            innerSegs_.push_back(P(centerX - boxHalfW, st.median));
            innerSegs_.push_back(P(centerX + boxHalfW, st.median));
        }
    }
}

void ViolinPlot::prepare(render::Renderer& r) {
    buildGeometry();
    auto& ctx = r.backend().context();

    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!bodyFillPos_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{bodyFillPos_}, std::span{bodyFillColors_});
    }

    edgeRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!bodyEdgeSegs_.empty()) {
        edgeRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{bodyEdgeSegs_}, cfg_.edgeColor,
                             cfg_.lineWidth);
    }

    innerRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                        r.backend().sampleCount(), r.pipelineCache());
    if (!innerSegs_.empty()) {
        innerRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{innerSegs_}, cfg_.whiskerColor,
                              cfg_.lineWidth);
    }

    prepared_ = true;
}

void ViolinPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!bodyFillPos_.empty())
        fillRenderer_.draw(cmd, vrect, t);
    if (!bodyEdgeSegs_.empty())
        edgeRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(bodyEdgeSegs_.size()));
    if (!innerSegs_.empty())
        innerRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(innerSegs_.size()));
}

void ViolinPlot::contributeToAutoscale(Viewport& v) const {
    // Position axis: group centers ± half width. Value axis: data range.
    Range& posAxis = cfg_.vert ? v.x : v.y;
    Range& valAxis = cfg_.vert ? v.y : v.x;
    for (size_t g = 0; g < groups_.size(); ++g) {
        float c = g < cfg_.positions.size() ? cfg_.positions[g]
                                            : float(g + 1);
        float hw = (g < cfg_.widths.size() ? cfg_.widths[g]
                                          : cfg_.width) * 0.5f;
        posAxis.min = std::min(posAxis.min, c - hw);
        posAxis.max = std::max(posAxis.max, c + hw);
        for (float val : groups_[g]) {
            valAxis.min = std::min(valAxis.min, val);
            valAxis.max = std::max(valAxis.max, val);
        }
    }
}

} // namespace volcano::plot
