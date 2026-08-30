// volcano/plot/Plot.cpp
#include "volcano/plot/Plot.hpp"

namespace volcano::plot {

void IPlot::contributeToAutoscaleGpu(
    [[maybe_unused]] render::primitives::ReduceRenderer& reducer,
    Viewport& v) const {
    // Default: fall back to the CPU autoscale contribution.
    contributeToAutoscale(v);
}

Figure::Figure(uint32_t rows, uint32_t cols) : rows_(rows), cols_(cols) {}

Axes* Figure::addAxes(uint32_t row, uint32_t col, uint32_t rowSpan, uint32_t colSpan) {
    auto a = std::make_unique<Axes>();
    Axes* raw = a.get();
    placements_.push_back({std::move(a), row, col, rowSpan, colSpan});
    return raw;
}

void Figure::layout(Extent2D extent) {
    const float margin = 0.08f;
    const float titleH = style_.title.text.empty() ? 0.0f : 0.06f;
    float cellW = (1.0f - 2*margin) / cols_;
    float cellH = (1.0f - 2*margin - titleH) / rows_;
    for (auto& p : placements_) {
        Rect2D r;
        r.x = static_cast<int32_t>((margin + p.c * cellW) * extent.width);
        r.y = static_cast<int32_t>((margin + titleH + p.r * cellH) * extent.height);
        r.width  = static_cast<uint32_t>(p.cs * cellW * extent.width);
        r.height = static_cast<uint32_t>(p.rs * cellH * extent.height);
        p.axes->rect = r;
    }
}

} // namespace volcano::plot
