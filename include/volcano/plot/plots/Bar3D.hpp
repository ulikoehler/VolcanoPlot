// volcano/plot/plots/Bar3D.hpp — 3D bar chart (matplotlib `bar3d`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Bar3D configuration.
struct Bar3DConfig {
    /// Fill color for all bars (used if perBarColors is empty).
    Color color = Color::fromRgba8(31, 119, 180, 200);
    /// Edge color for bar outlines.
    Color edgeColor = Color::black();
    /// Edge line width.
    float edgeWidth = 1.0f;
    /// Whether to draw edges.
    bool drawEdges = true;
    /// Label for legend.
    std::string label;
};

/// 3D bar chart — renders rectangular bars in 3D space. Equivalent to
/// matplotlib's `bar3d`.
///
/// Each bar is defined by a base position (x, y, z) and dimensions
/// (dx, dy, dz). The 6 faces of each box are projected to 2D NDC using
/// the Camera3D view-projection matrix and rendered as filled polygons
/// via FillRenderer. Faces are sorted by depth (painter's algorithm)
/// for correct occlusion. Optional edge outlines are drawn via
/// LineRenderer.
class Bar3D : public IPlot {
public:
    /// Construct from base positions and dimensions.
    /// x, y, z: base corner of each bar.
    /// dx, dy, dz: dimensions of each bar.
    Bar3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
          std::vector<float> dx, std::vector<float> dy, std::vector<float> dz,
          Bar3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<float> x_, y_, z_, dx_, dy_, dz_;
    Bar3DConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;

    render::primitives::LineSegmentRenderer lineRenderer_;
    std::vector<Point2D> edgePoints_;

    bool prepared_ = false;

    void projectBars();
};

} // namespace volcano::plot
