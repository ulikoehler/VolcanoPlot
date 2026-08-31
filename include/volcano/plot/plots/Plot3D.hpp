// volcano/plot/plots/Plot3D.hpp — 3D line plot (matplotlib `plot3D`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Plot3D configuration.
struct Plot3DConfig {
    /// Line color.
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Line width.
    float lineWidth = 1.5f;
    /// Line style (Solid, Dashed, etc.).
    LineStyle lineStyle = LineStyle::Solid;
    /// Whether to show markers at each point.
    bool showMarkers = false;
    /// Marker color.
    Color markerColor = Color::fromRgba8(31, 119, 180, 255);
    /// Marker size in pixels.
    float markerSize = 6.0f;
    /// Marker style.
    MarkerStyle markerStyle = MarkerStyle::Circle;
    /// Label for legend.
    std::string label;
};

/// 3D line plot — renders a polyline in 3D space. Equivalent to
/// matplotlib's `plot3D` / `Axes3D.plot`.
///
/// The 3D points are projected to 2D screen space on the CPU using
/// the Camera3D view-projection matrix, then rendered as a line strip
/// via LineRenderer. Optional markers are rendered via PointRenderer.
class Plot3D : public IPlot {
public:
    /// Construct from (x, y, z) arrays.
    Plot3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
           Plot3DConfig config = {});

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
    std::vector<float> x_, y_, z_;
    Plot3DConfig config_;
    Camera3D camera_;

    render::primitives::LineRenderer lineRenderer_;
    std::vector<Point2D> projectedPoints_;

    render::primitives::PointRenderer pointRenderer_;
    std::vector<Point2D> markerPoints_;
    std::vector<Color> markerColors_;

    bool prepared_ = false;

    void projectPoints();
};

} // namespace volcano::plot
