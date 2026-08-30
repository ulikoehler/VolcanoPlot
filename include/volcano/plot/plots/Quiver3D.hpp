// volcano/plot/plots/Quiver3D.hpp — 3D vector field plot (matplotlib `Axes3D.quiver`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for Quiver3D.
struct Quiver3DConfig {
    /// Arrow color.
    Color color = Color::black();
    /// Shaft line width.
    float lineWidth = 1.0f;
    /// Scale factor for arrow lengths. 1.0 = use vector as-is.
    float scale = 1.0f;
    /// Arrowhead length in NDC units (relative to canvas).
    float headLength = 0.02f;
    /// Arrowhead width in NDC units.
    float headWidth = 0.015f;
    /// Whether to draw arrowheads as filled triangles.
    bool filledHeads = true;
    /// Label for legend.
    std::string label;
};

/// 3D quiver (vector field) plot. Draws 3D arrows at specified positions
/// representing a 3D vector field. Equivalent to matplotlib's
/// `Axes3D.quiver(x, y, z, u, v, w)`.
///
/// Each arrow consists of a shaft (line from base to tip) and an arrowhead
/// (filled triangle). All 3D points are projected to 2D NDC on the CPU using
/// the Camera3D view-projection matrix. The arrowhead direction is computed
/// in screen space based on the projected shaft direction.
class Quiver3D : public IPlot {
public:
    /// Construct from positions (x, y, z) and vectors (u, v, w).
    Quiver3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
             std::vector<float> u, std::vector<float> v, std::vector<float> w,
             Quiver3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }

private:
    std::vector<float> x_, y_, z_, u_, v_, w_;
    Quiver3DConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer shaftRenderer_;
    std::vector<Point2D> shaftSegments_;

    render::primitives::FillRenderer headRenderer_;
    std::vector<Point2D> headPositions_;
    std::vector<Color> headColors_;

    bool prepared_ = false;

    void projectArrows();
};

} // namespace volcano::plot
