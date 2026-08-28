// volcano/plot/plots/VolcanoPlot.cpp
#include "volcano/plot/plots/VolcanoPlot.hpp"
#include <cmath>
#include <algorithm>
namespace volcano::plot {

VolcanoPlot::VolcanoPlot(VolcanoData data)
    : data_(std::move(data)),
      scatter_([&]{
          Series2D s;
          s.label = "Volcano";
          s.points.resize(data_.log2FoldChange.size());
          s.color = data_.nsColor;
          s.size = 4.0f;
          s.marker = MarkerStyle::Circle;
          // We'll color per-point in prepare() via the scatter's color buffer.
          for (size_t i = 0; i < data_.log2FoldChange.size(); ++i) {
              s.points[i] = { data_.log2FoldChange[i], -std::log10(std::max(data_.pValue[i], 1e-300f)) };
          }
          return s;
      }()) {}

void VolcanoPlot::prepare(render::Renderer& r) {
    // Override scatter colors based on significance.
    auto& series = scatter_.series();
    // Note: current PointRenderer uses a single color buffer; we'd need per-point
    // colors. For now, use the nsColor; a future enhancement will pass per-point colors.
    scatter_.prepare(r);
}

void VolcanoPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    scatter_.draw(cmd, r, axes, rect);
}

void VolcanoPlot::contributeToAutoscale(Viewport& v) const {
    scatter_.contributeToAutoscale(v);
}

} // namespace volcano::plot
