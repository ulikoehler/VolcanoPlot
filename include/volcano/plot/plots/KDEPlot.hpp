// volcano/plot/plots/KDEPlot.hpp — Kernel Density Estimation plot (GPU-side)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/HeatmapRenderer.hpp"
#include "volcano/render/primitives/KdeEvalRenderer.hpp"
namespace volcano::plot {

/// KDE plot: streams raw samples to the GPU, which evaluates a kernel density
/// estimate into a 2D grid (compute shader), then renders as a heatmap.
class KDEPlot : public IPlot {
public:
    KDEPlot(std::vector<Point2D> samples, uint32_t gridW = 256, uint32_t gridH = 256,
            float bandwidth = 0.0f, const Colormap& cmap = colormaps::viridis())
        : samples_(std::move(samples)), cmap_(cmap), gridW_(gridW), gridH_(gridH),
          bandwidth_(bandwidth) {}
    /// mpl-style fixed evaluation range (e.g. np.mgrid[-4:4,-4:4]).
    /// When set, density is evaluated on this grid instead of the data range.
    KDEPlot& evalRange(Range x, Range y) { evalX_ = x; evalY_ = y; return *this; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// mpl sticky edges: tight autoscale, no 5% margin.
    [[nodiscard]] bool tightAutoscale() const override { return true; }
    /// Density range (drives the colorbar).
    [[nodiscard]] std::optional<Range> valueRange() const override {
        return grid_.valueRange.valid() ? std::optional{grid_.valueRange}
                                        : std::nullopt;
    }

private:
    std::vector<Point2D> samples_;
    const Colormap& cmap_;
    uint32_t gridW_, gridH_;
    float bandwidth_; // 0 = auto (Scott's rule, scipy gaussian_kde default)
    Range evalX_{1.0f, 0.0f}, evalY_{1.0f, 0.0f}; // invalid → data range
    Grid2D grid_;
    render::primitives::HeatmapRenderer renderer_;
    render::primitives::KdeEvalRenderer kde_;
    bool prepared_ = false;
    bool kdeInited_ = false;

    void evaluateKdeOnGpu(render::Renderer& r);
};

} // namespace volcano::plot
