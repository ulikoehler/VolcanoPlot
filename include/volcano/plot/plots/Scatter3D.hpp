// volcano/plot/plots/Scatter3D.hpp — 3D scatter plot (matplotlib `scatter3D`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Scatter3D configuration.
struct Scatter3DConfig {
    /// Marker color (used for all points if perPointColors is empty).
    Color color = Color::fromRgba8(31, 119, 180, 255);
    /// Marker size in pixels (used for all points if perPointSizes is empty).
    float size = 6.0f;
    /// Marker style.
    MarkerStyle markerStyle = MarkerStyle::Circle;
    /// Label for legend.
    std::string label;
};

/// 3D scatter plot — renders points in 3D space. Equivalent to
/// matplotlib's `scatter3D` / `Axes3D.scatter`.
///
/// The 3D points are projected to 2D NDC on the CPU using the Camera3D
/// view-projection matrix, then rendered as markers via PointRenderer.
/// Per-point colors and sizes are supported.
class Scatter3D : public IPlot {
public:
    /// Construct from (x, y, z) arrays with uniform color/size.
    Scatter3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
               Scatter3DConfig config = {});

    /// Construct with per-point colors.
    Scatter3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
              std::vector<Color> colors, Scatter3DConfig config = {});

    /// Construct with per-point colors and sizes.
    Scatter3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
              std::vector<Color> colors, std::vector<float> sizes,
              Scatter3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<float> x_, y_, z_;
    std::vector<Color> perPointColors_;
    std::vector<float> perPointSizes_;
    Scatter3DConfig config_;
    Camera3D camera_;

    render::primitives::PointRenderer pointRenderer_;
    std::vector<Point2D> projectedPoints_;
    std::vector<Color> markerColors_;
    std::vector<float> markerSizes_;

    bool prepared_ = false;

    void projectPoints();
};

} // namespace volcano::plot
