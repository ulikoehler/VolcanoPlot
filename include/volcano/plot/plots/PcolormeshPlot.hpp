// volcano/plot/plots/PcolormeshPlot.hpp — pseudocolor mesh plot
// (matplotlib `pcolormesh`, `pcolor`, `pcolorfast`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for PcolormeshPlot.
struct PcolormeshConfig {
    /// Colormap for cell coloring. If nullptr, uses viridis.
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    Range valueRange{0, 0};
    /// Optional edge color for cell borders. If alpha=0, no edges.
    Color edgeColor = Color::transparent();
    float edgeWidth = 1.0f;
    /// If true, cells with NaN values are skipped (transparent).
    bool skipNaN = true;
    std::string label;
};

/// Pseudocolor mesh plot — rectangular cells with per-cell colors from a
/// colormap. Equivalent to matplotlib's `pcolormesh(x, y, C)`.
///
/// Unlike HeatmapPlot (which uses a GPU texture), PcolormeshPlot builds
/// explicit triangle geometry for each cell, allowing:
///   - Non-uniform cell sizes (irregular x/y edges)
///   - Per-cell edge drawing
///   - NaN cell skipping
///
/// `x` has N+1 elements (cell edges), `y` has M+1 elements,
/// `C` has N*M values (row-major, C[j*N + i] is cell (i, j)).
class PcolormeshPlot : public IPlot {
public:
    /// Construct from cell edges and values.
    /// x: N+1 vertical edge coordinates, y: M+1 horizontal edge coordinates,
    /// C: N*M cell values (row-major, C[j*N + i] = cell at column i, row j).
    PcolormeshPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> C, uint32_t nCols, uint32_t nRows,
                   PcolormeshConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

    /// The computed value range (valid after prepare()).
    [[nodiscard]] Range valueRange() const { return valueRange_; }

private:
    std::vector<float> x_, y_, C_;
    uint32_t nCols_, nRows_;
    PcolormeshConfig config_;
    Range valueRange_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeValueRange();
    void buildGeometry();
};

} // namespace volcano::plot
