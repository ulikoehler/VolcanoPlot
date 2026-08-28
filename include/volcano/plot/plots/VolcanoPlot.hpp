// volcano/plot/plots/VolcanoPlot.hpp — genomics volcano plot
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/plots/ScatterPlot.hpp"
namespace volcano::plot {

/// Volcano plot data: log2 fold change vs -log10 p-value, with significance
/// thresholds coloring points as up/down/non-significant.
struct VolcanoData {
    std::vector<float> log2FoldChange;
    std::vector<float> pValue;
    float log2FcThreshold = 1.0f;   // |log2FC| > threshold => significant
    float pValueThreshold = 0.05f;  // p < threshold => significant
    Color upColor = Color::red();
    Color downColor = Color::blue();
    Color nsColor = Color::fromRgba8(180, 180, 180);
};

class VolcanoPlot : public IPlot {
public:
    explicit VolcanoPlot(VolcanoData data);
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return "Volcano"; }

private:
    VolcanoData data_;
    ScatterPlot scatter_;
};

} // namespace volcano::plot
