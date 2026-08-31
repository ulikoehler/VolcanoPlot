// volcano/plot/plots/PcolorfastPlot.hpp — fast pseudocolor plot
// (matplotlib `pcolorfast`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace volcano::plot {

/// Pcolorfast configuration (same as PcolormeshConfig).
struct PcolorfastConfig {
    /// Colormap for cell coloring. If nullptr, uses viridis.
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    /// Ignored if norm is set (use norm->setVmin/setVmax instead).
    Range valueRange{0, 0};
    /// Optional normalization. If set, replaces the linear (vmin,vmax) mapping.
    std::shared_ptr<Normalize> norm;
    /// If true, cells with NaN values are skipped (transparent).
    bool skipNaN = true;
    std::string label;
};

/// Fast pseudocolor plot — equivalent to matplotlib's `pcolorfast`.
///
/// `pcolorfast` is optimized for regular grids. It accepts either:
/// 1. Explicit cell edges (like pcolormesh): x has N+1, y has M+1
/// 2. An extent (xMin, xMax, yMin, yMax) with grid dimensions
///
/// For regular grids, it generates uniform cell edges from the extent.
/// Rendering uses FillRenderer with per-cell colormap colors.
class PcolorfastPlot : public IPlot {
public:
    /// Construct from explicit cell edges and values.
    /// x: N+1 vertical edge coordinates, y: M+1 horizontal edge coordinates,
    /// C: N*M cell values (row-major, C[j*N + i] = cell at column i, row j).
    PcolorfastPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> C, uint32_t nCols, uint32_t nRows,
                   PcolorfastConfig config = {});

    /// Construct from extent (regular grid).
    /// C: nCols*nRows cell values (row-major).
    /// extent: (xMin, xMax, yMin, yMax).
    PcolorfastPlot(std::vector<float> C, uint32_t nCols, uint32_t nRows,
                   Range xRange, Range yRange,
                   PcolorfastConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, C_;
    uint32_t nCols_, nRows_;
    PcolorfastConfig config_;
    Range valueRange_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;

    void computeValueRange();
    void buildGeometry();
};

} // namespace volcano::plot
