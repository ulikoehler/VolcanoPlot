// volcano/plot/plots/TrisurfPlot.hpp — 3D triangulated surface (matplotlib `plot_trisurf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Trisurf configuration.
struct TrisurfConfig {
    /// Colormap for triangle coloring (nullptr = viridis).
    const Colormap* cmap = nullptr;
    /// Explicit value range for color mapping. If invalid, computed from z data.
    Range valueRange{0, 0};
    /// Edge color for triangle outlines (transparent = no edges).
    Color edgeColor = Color::transparent();
    /// Edge line width.
    float edgeWidth = 0.5f;
    /// Whether to draw triangle edges.
    bool drawEdges = false;
    /// Label for legend.
    std::string label;
};

/// 3D triangulated surface plot — renders a surface defined by scattered
/// 3D points. Equivalent to matplotlib's `plot_trisurf`.
///
/// The (x, y) coordinates are Delaunay-triangulated to form triangles,
/// and z is used as the surface height. Each triangle is projected to 2D
/// NDC on the CPU using the Camera3D view-projection matrix, then rendered
/// as a filled polygon via FillRenderer. Triangles are sorted by average
/// depth (painter's algorithm) for correct occlusion. Colors are mapped
/// from the average z-value of each triangle through a colormap.
/// Optional edge outlines are drawn via LineSegmentRenderer.
class TrisurfPlot : public IPlot {
public:
    /// Construct from (x, y, z) arrays.
    TrisurfPlot(std::vector<float> x, std::vector<float> y, std::vector<float> z,
                TrisurfConfig config = {});

    /// Construct with explicit triangles.
    TrisurfPlot(std::vector<float> x, std::vector<float> y, std::vector<float> z,
                std::vector<Triangle> triangles, TrisurfConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> triangles_;
    TrisurfConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;

    render::primitives::LineSegmentRenderer edgeRenderer_;
    std::vector<Point2D> edgeSegments_;

    bool prepared_ = false;

    void projectSurface();
};

} // namespace volcano::plot
