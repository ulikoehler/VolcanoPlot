// volcano/plot/plots/StackPlot.cpp — stacked area plot implementation
#include "volcano/plot/plots/StackPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
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

void StackPlot::computeStack() {
    size_t n = x_.size();
    size_t numSeries = ys_.size();
    stack_.assign(numSeries + 1, std::vector<float>(n, 0.0f));
    // stack_[0] = baseline (0).
    for (size_t s = 0; s < numSeries; ++s)
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

void StackPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void StackPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    // Y range is the total stack height.
    if (!stack_.empty()) {
        const auto& top = stack_.back();
        for (float yv : top) {
            v.y.min = std::min(v.y.min, yv);
            v.y.max = std::max(v.y.max, yv);
        }
        // Also include baseline (0).
        v.y.min = std::min(v.y.min, 0.0f);
        v.y.max = std::max(v.y.max, 0.0f);
    } else {
        // If stack not computed yet, compute from raw data.
        for (const auto& y : ys_)
            for (float yv : y) {
                v.y.min = std::min(v.y.min, yv);
                v.y.max = std::max(v.y.max, yv);
            }
    }
}

} // namespace volcano::plot
