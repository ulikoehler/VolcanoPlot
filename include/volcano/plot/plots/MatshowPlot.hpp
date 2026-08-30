// volcano/plot/plots/MatshowPlot.hpp — matrix display (matplotlib `matshow`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Matshow configuration.
struct MatshowConfig {
    /// Colormap for cell coloring (nullptr = viridis).
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    Range valueRange{0, 0};
    /// Label for legend (typically empty).
    std::string label;
};

/// Matrix display plot — displays a 2D array as a color-coded image with
/// row 0 at the top (matrix convention). Equivalent to matplotlib's `matshow`.
///
/// Each cell is rendered as a filled rectangle colored by the colormap
/// based on the cell's value. No interpolation (nearest-neighbor display).
class MatshowPlot : public IPlot {
public:
    /// Construct from a 2D matrix (row-major, nrows × ncols).
    MatshowPlot(std::vector<float> data, uint32_t nrows, uint32_t ncols,
                MatshowConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> data_;
    uint32_t nrows_, ncols_;
    MatshowConfig config_;
    Range valueRange_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void buildGeometry();
};

} // namespace volcano::plot
