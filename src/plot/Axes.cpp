// volcano/plot/Axes.cpp
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"

namespace volcano::plot {

IPlot* Axes::addPlot(std::unique_ptr<IPlot> plot) {
    IPlot* raw = plot.get();
    plots_.push_back(std::move(plot));
    return raw;
}

void Axes::autoscale() {
    if (manualViewport_) return;
    Viewport v{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                0, 1 };
    for (const auto& p : plots_) p->contributeToAutoscale(v);
    if (v.x.min > v.x.max) { v.x = {0,1}; v.y = {0,1}; }
    // 5% padding
    float padx = v.x.span() * 0.05f;
    float pady = v.y.span() * 0.05f;
    v.x.min -= padx; v.x.max += padx;
    v.y.min -= pady; v.y.max += pady;
    viewport_ = v;
}

} // namespace volcano::plot
