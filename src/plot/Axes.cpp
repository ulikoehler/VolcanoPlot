// volcano/plot/Axes.cpp
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"

#include <limits>

namespace volcano::plot {

IPlot* Axes::addPlot(std::unique_ptr<IPlot> plot) {
    IPlot* raw = plot.get();
    plots_.push_back(std::move(plot));
    return raw;
}

void Axes::finalizeAutoscale(Viewport& v) {
    // Check each axis independently — a plot may only contribute to one
    // axis (e.g., AxhLine only contributes y, AxvLine only contributes x).
    if (v.x.min > v.x.max) v.x = {0,1};
    if (v.y.min > v.y.max) v.y = {0,1};
    // Handle zero-span axes (e.g., single horizontal line at one y value).
    if (v.x.span() == 0) { v.x.min -= 0.5f; v.x.max += 0.5f; }
    if (v.y.span() == 0) { v.y.min -= 0.5f; v.y.max += 0.5f; }
    // 5% padding
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
}

void Axes::autoscale() {
    if (manualViewport_) return;
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                0, 1 };
    for (const auto& p : plots_) p->contributeToAutoscale(v);
    finalizeAutoscale(v);
    viewport_ = v;
}

void Axes::autoscaleGpu(render::primitives::ReduceRenderer& reducer) {
    if (manualViewport_) return;
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                0, 1 };
    // Each layer contributes via GPU reduce where possible, falling back to
    // CPU per-layer (the default IPlot::contributeToAutoscaleGpu behavior).
    for (const auto& p : plots_) p->contributeToAutoscaleGpu(reducer, v);
    finalizeAutoscale(v);
    viewport_ = v;
}

} // namespace volcano::plot
