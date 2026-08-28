// volcano/plot/plots/KDEPlot.hpp — Kernel Density Estimation plot (GPU-side)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/HeatmapRenderer.hpp"
namespace volcano::plot {

/// KDE plot: streams raw samples to the GPU, which evaluates a kernel density
/// estimate into a 2D grid (compute shader), then renders as a heatmap.
class KDEPlot : public IPlot {
public:
    KDEPlot(std::vector<Point2D> samples, uint32_t gridW = 256, uint32_t gridH = 256,
            float bandwidth = 0.0f, const Colormap& cmap = colormaps::viridis())
        : samples_(std::move(samples)), cmap_(cmap), gridW_(gridW), gridH_(gridH),
          bandwidth_(bandwidth) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;

private:
    std::vector<Point2D> samples_;
    const Colormap& cmap_;
    uint32_t gridW_, gridH_;
    float bandwidth_; // 0 = auto (Silverman's rule)
    Grid2D grid_;
    render::primitives::HeatmapRenderer renderer_;
    bool prepared_ = false;

    void evaluateKdeOnGpu(render::Renderer& r);
};

} // namespace volcano::plot
