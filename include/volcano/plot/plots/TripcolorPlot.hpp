// volcano/plot/plots/TripcolorPlot.hpp — pseudocolor on unstructured
// triangular grids (matplotlib `tripcolor`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace volcano::plot {

/// Shading mode for tripcolor.
enum class TriShading {
    Flat,       ///< One color per triangle (average of vertex values).
    Gouraud,    ///< Per-vertex colors with barycentric interpolation.
};

/// Tripcolor configuration.
struct TripcolorConfig {
    /// Shading mode.
    TriShading shading = TriShading::Flat;
    /// Colormap for coloring. If nullptr, uses viridis.
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from data.
    /// Ignored if norm is set (use norm->setVmin/setVmax instead).
    Range valueRange{0, 0};
    /// Optional normalization. If set, replaces the linear (vmin,vmax) mapping.
    std::shared_ptr<Normalize> norm;
    /// Optional edge color for triangle borders. If alpha=0, no edges.
    Color edgeColor = Color::transparent();
    float edgeWidth = 1.0f;
    /// Label for legend.
    std::string label;
};

/// Pseudocolor plot on unstructured triangular grids.
/// Delaunay-triangulates the (x,y) points, then colors each triangle
/// based on per-vertex or per-face values. Equivalent to matplotlib's
/// `tripcolor`.
class TripcolorPlot : public IPlot {
public:
    /// Construct from per-vertex values (x, y, z).
    /// For flat shading, each triangle's color is the average of its
    /// 3 vertex z values. For gouraud, per-vertex colors are used.
    TripcolorPlot(std::vector<float> x, std::vector<float> y,
                  std::vector<float> z, TripcolorConfig config = {});

    /// Construct from per-face values (x, y, triangles, facevalues).
    /// `triangles` specifies the triangulation directly.
    /// `facevalues` has one value per triangle.
    TripcolorPlot(std::vector<float> x, std::vector<float> y,
                  std::vector<Triangle> triangles,
                  std::vector<float> facevalues,
                  TripcolorConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> tris_;
    std::vector<float> facevalues_;
    bool useFacevalues_ = false;
    TripcolorConfig config_;
    Range valueRange_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> positions_;
    std::vector<Color> colors_;
    bool prepared_ = false;

    void computeValueRange();
    void buildGeometry();
};

} // namespace volcano::plot
