// volcano/plot/plots/StackPlot.cpp — stacked area plot implementation
#include "volcano/plot/plots/StackPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "../VectorEmitHelpers.hpp"
#include <algorithm>
#include <format>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Default color palette (tab10 first 6 colors).
constexpr Color kDefaultColors[] = {
    Color::fromRgba8(31, 119, 180, 180),
    Color::fromRgba8(255, 127, 14, 180),
    Color::fromRgba8(44, 160, 44, 180),
    Color::fromRgba8(214, 39, 40, 180),
    Color::fromRgba8(148, 103, 189, 180),
    Color::fromRgba8(140, 86, 75, 180),
};
constexpr size_t kNumDefaultColors = std::size(kDefaultColors);

} // namespace

StackPlot::StackPlot(std::vector<float> x,
                     std::vector<std::vector<float>> ys,
                     std::vector<Color> colors,
                     std::vector<std::string> labels)
    : x_(std::move(x)), ys_(std::move(ys)),
      colors_(std::move(colors)), labels_(std::move(labels)) {
    if (x_.empty() || ys_.empty())
        throw std::invalid_argument("StackPlot: x and ys must not be empty");
    for (size_t i = 0; i < ys_.size(); ++i)
        if (ys_[i].size() != x_.size())
            throw std::invalid_argument("StackPlot: all y series must match x size");
    initDefaultColors();
}

void StackPlot::initDefaultColors() {
    if (colors_.empty() || colors_.size() < ys_.size()) {
        colors_.resize(ys_.size());
        for (size_t i = 0; i < ys_.size(); ++i) {
            if (i < kNumDefaultColors)
                colors_[i] = kDefaultColors[i];
            else
                colors_[i] = kDefaultColors[i % kNumDefaultColors];
        }
    }
}

Color StackPlot::legendColor() const {
    return colors_.empty() ? Color::blue() : colors_[0];
}

void StackPlot::setBaseline(std::string_view name) {
    if (name == "zero") baseline_ = StackBaseline::Zero;
    else if (name == "sym") baseline_ = StackBaseline::Sym;
    else if (name == "wiggle") baseline_ = StackBaseline::Wiggle;
    else if (name == "weighted_wiggle")
        baseline_ = StackBaseline::WeightedWiggle;
    else
        throw std::invalid_argument(
            std::format("StackPlot: unknown baseline '{}'", name));
    touch();
}

void StackPlot::computeStack() const {
    const size_t n = x_.size();
    const size_t m = ys_.size();
    stack_.assign(m + 1, std::vector<float>(n, 0.0f));
    // stack_[0] = the baseline — mpl Axes.stackplot `first_line`.
    auto& first = stack_[0];
    switch (baseline_) {
    case StackBaseline::Zero:
        break;
    case StackBaseline::Sym:
        // first_line = -sum(y, 0) * 0.5
        for (size_t i = 0; i < n; ++i) {
            float s = 0;
            for (const auto& y : ys_) s += y[i];
            first[i] = -0.5f * s;
        }
        break;
    case StackBaseline::Wiggle:
        // first_line = -(1/m) * Σ_s y_s · (m - 0.5 - s)
        for (size_t i = 0; i < n; ++i) {
            float acc = 0;
            for (size_t s = 0; s < m; ++s)
                acc += ys_[s][i] * (float(m) - 0.5f - float(s));
            first[i] = -acc / float(m);
        }
        break;
    case StackBaseline::WeightedWiggle: {
        // mpl: total = sum(y,0); increase = [y[:,0], diff(y)];
        // below_size = total - cumsum(y) + 0.5·y; move_up =
        // below_size/total (0.5 at column 0); center = cumsum over x
        // of Σ_s (move_up - 0.5)·increase; first_line = center - total/2.
        float acc = 0;  // running cumsum of the center contribution
        for (size_t i = 0; i < n; ++i) {
            float total = 0;
            for (const auto& y : ys_) total += y[i];
            float inv = total > 0 ? 1.0f / total : 0.0f;
            float cum = 0;  // cumsum over series at column i
            float sum = 0;
            for (size_t s = 0; s < m; ++s) {
                cum += ys_[s][i];
                float below = total - cum + 0.5f * ys_[s][i];
                float moveUp = (i == 0) ? 0.5f : below * inv;
                float increase = (i == 0) ? ys_[s][0]
                                          : ys_[s][i] - ys_[s][i - 1];
                sum += (moveUp - 0.5f) * increase;
            }
            acc += sum;
            first[i] = acc - 0.5f * total;
        }
        break;
    }
    }
    for (size_t s = 0; s < m; ++s)
        for (size_t i = 0; i < n; ++i)
            stack_[s + 1][i] = stack_[s][i] + ys_[s][i];
}

void StackPlot::buildFillTriangles() {
    fillPositions_.clear();
    fillColors_.clear();
    size_t n = x_.size();
    if (n < 2) return;
    size_t numSeries = ys_.size();

    for (size_t s = 0; s < numSeries; ++s) {
        const auto& lo = stack_[s];      // lower boundary
        const auto& hi = stack_[s + 1];  // upper boundary
        Color color = colors_[s];

        for (size_t i = 0; i + 1 < n; ++i) {
            Point2D ul{x_[i],   hi[i]};
            Point2D ur{x_[i+1], hi[i+1]};
            Point2D ll{x_[i],   lo[i]};
            Point2D lr{x_[i+1], lo[i+1]};
            // Triangle 1: ul, ur, ll
            fillPositions_.push_back(ul);
            fillPositions_.push_back(ur);
            fillPositions_.push_back(ll);
            // Triangle 2: ur, lr, ll
            fillPositions_.push_back(ur);
            fillPositions_.push_back(lr);
            fillPositions_.push_back(ll);
            for (int j = 0; j < 6; ++j) fillColors_.push_back(color);
        }
    }
}

void StackPlot::prepare(render::Renderer& r) {
    computeStack();
    buildFillTriangles();
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

void StackPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect = clipRectVk(rect, r.backend().extent());
    fillRenderer_.draw(cmd, vrect, t);
}

void StackPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                           Rect2D rect) {
    computeStack();
    auto toPx = pxMapper(axes, rect);
    const size_t n = x_.size(), m = ys_.size();
    if (n < 2) return;
    for (size_t s = 0; s < m; ++s) {
        std::vector<Point2D> poly;
        poly.reserve(n * 2);
        for (size_t i = 0; i < n; ++i)
            poly.push_back(toPx({x_[i], stack_[s + 1][i]}));
        for (size_t i = n; i-- > 0;)
            poly.push_back(toPx({x_[i], stack_[s][i]}));
        c.polygon(poly, colors_[s]);
    }
}

void StackPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    // Y range covers every stack boundary — the baseline can be
    // negative ('sym', 'wiggle', 'weighted_wiggle' baselines).
    if (stack_.empty()) computeStack();
    for (const auto& row : stack_)
        for (float yv : row) {
            v.y.min = std::min(v.y.min, yv);
            v.y.max = std::max(v.y.max, yv);
        }
}

} // namespace volcano::plot
