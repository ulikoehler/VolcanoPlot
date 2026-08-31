// volcano/plot/plots/HexbinPlot.hpp — hexagonal binning (matplotlib `hexbin`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <array>

namespace volcano::plot {

/// Hexagonal grid orientation.
enum class HexOrientation {
    PointyTop,  ///< Hexagons with a point at the top
    FlatTop,    ///< Hexagons with a flat edge at the top
};

/// Hexbin normalization mode.
enum class HexbinNorm {
    Count,       ///< raw counts (default)
    Density,     ///< count / total — sums to 1
};

/// Hexbin configuration.
struct HexbinConfig {
    /// Number of hexagons along the x-axis (approximate).
    int gridsize = 50;
    /// Colormap for cell coloring (nullptr = viridis).
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    /// Ignored if norm is set (use norm->setVmin/setVmax instead).
    Range valueRange{0, 0};
    /// Optional normalization. If set, replaces the linear (vmin,vmax) mapping.
    std::shared_ptr<Normalize> norm;
    /// Hexagon orientation.
    HexOrientation orientation = HexOrientation::PointyTop;
    /// Normalization mode.
    HexbinNorm normMode = HexbinNorm::Count;
    /// Minimum count to display a cell. Cells with fewer points are skipped.
    int minCount = 1;
    /// Optional edge color for hex borders. If alpha=0, no edges.
    Color edgeColor = Color::transparent();
    float edgeWidth = 1.0f;
    std::string label;
};

/// Hexagonal binning plot. Bins (x, y) sample pairs into hexagonal cells
/// and renders them colored by count/density.
/// Equivalent to matplotlib's `hexbin(x, y, gridsize=N)`.
class HexbinPlot : public IPlot {
public:
    /// Construct from raw (x, y) sample pairs.
    HexbinPlot(std::vector<float> x, std::vector<float> y,
               HexbinConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_;
    HexbinConfig config_;

    // Computed in prepare().
    float hexRadius_ = 0.0f;   // distance from center to vertex
    float xMin_ = 0, xMax_ = 0, yMin_ = 0, yMax_ = 0;
    Range valueRange_;

    // Hex cell centers and counts.
    std::vector<Point2D> centers_;
    std::vector<float> counts_;

    // Render data.
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    render::primitives::FillRenderer fillRenderer_;
    bool prepared_ = false;

    void computeBins();
    void buildGeometry();

    /// Get the 6 vertices of a hexagon at center (cx, cy) with given radius.
    std::array<Point2D, 6> hexVertices(float cx, float cy, float r) const;
};

} // namespace volcano::plot
