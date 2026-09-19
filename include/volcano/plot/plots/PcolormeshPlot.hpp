// volcano/plot/plots/PcolormeshPlot.hpp — pseudocolor mesh plot
// (matplotlib `pcolormesh`, `pcolor`, `pcolorfast`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace volcano::plot {

/// mpl pcolormesh `shading`: how cell values map onto the mesh.
enum class PcmShading {
    Flat,    ///< One color per cell; x/y are cell edges (N+1 x M+1).
    Gouraud, ///< Per-corner colors interpolated across each quad;
             ///< x/y are corner coordinates (N x M, same shape as C).
};

/// Configuration for PcolormeshPlot.
struct PcolormeshConfig {
    /// Shading mode (matplotlib `shading='flat'`/`'gouraud'`).
    PcmShading shading = PcmShading::Flat;
    /// Colormap for cell coloring. If nullptr, uses viridis.
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    /// Ignored if norm is set (use norm->setVmin/setVmax instead).
    Range valueRange{0, 0};
    /// Optional normalization. If set, replaces the linear (vmin,vmax) mapping.
    std::shared_ptr<Normalize> norm;
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
/// For `PcmShading::Flat` (default), `x` has N+1 elements (cell edges),
/// `y` has M+1 elements, `C` has N*M values (row-major, C[j*N + i] is
/// cell (i, j)). For `PcmShading::Gouraud`, `x`/`y`/`C` all share the
/// same N x M corner shape — matching matplotlib's shape rules.
class PcolormeshPlot : public IPlot {
public:
    /// Construct from cell edges (flat) or corner coordinates (gouraud)
    /// and values.
    /// Flat:    x: N+1 vertical edges, y: M+1 horizontal edges,
    ///          C: N*M cell values (row-major, C[j*N + i] = cell (i, j)).
    /// Gouraud: x: N corner x-coords, y: M corner y-coords,
    ///          C: N*M corner values; draws (N-1)*(M-1) quads.
    PcolormeshPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> C, uint32_t nCols, uint32_t nRows,
                   PcolormeshConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// mpl sticky edges: tight autoscale, no 5% margin.
    [[nodiscard]] bool tightAutoscale() const override { return true; }
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

    /// The computed value range (valid after prepare()).
    [[nodiscard]] std::optional<Range> valueRange() const override {
        return valueRange_.valid() ? std::optional{valueRange_} : std::nullopt;
    }

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
