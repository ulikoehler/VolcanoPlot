// volcano/plot/plots/WireframePlot.hpp — 3D wireframe plot (matplotlib `plot_wireframe`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Wireframe configuration.
struct WireframeConfig {
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.0f;
    /// Stride for row lines (1 = every row).
    uint32_t rowStride = 1;
    /// Stride for column lines (1 = every column).
    uint32_t colStride = 1;
    /// Label for legend.
    std::string label;
};

/// 3D wireframe plot — renders a 3D surface as a wireframe grid.
/// Equivalent to matplotlib's `plot_wireframe`.
///
/// The surface is defined by a Grid2D where values are z-heights.
/// The wireframe consists of line segments connecting adjacent grid
/// points along rows (constant y) and columns (constant x).
/// All 3D points are projected to 2D NDC on the CPU using the Camera3D
/// view-projection matrix, then rendered as line segments via
/// LineSegmentRenderer.
class WireframePlot : public IPlot {
public:
    /// Construct from a Grid2D (values = z-heights, xRange/yRange = axes).
    WireframePlot(Grid2D grid, WireframeConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

private:
    Grid2D grid_;
    WireframeConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer lineRenderer_;
    std::vector<Point2D> segments_;
    bool prepared_ = false;

    void projectWireframe();
};

} // namespace volcano::plot
