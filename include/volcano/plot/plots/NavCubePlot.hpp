// volcano/plot/plots/NavCubePlot.hpp — 3D orientation indicator overlay
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include <string>

namespace volcano::plot {

/// Corner of the axes rect where the indicator is drawn.
enum class NavCubeCorner {
    UpperLeft, UpperRight, LowerLeft, LowerRight
};

/// What to draw: a three-arrow axis triad, or a wireframe cube with
/// colored axis arrows (Blender/Paraview-style navigation cube).
enum class NavCubeMode {
    Triad,   ///< Three axis arrows (X/Y/Z) only.
    Cube     ///< Wireframe cube + axis arrows.
};

/// NavCube configuration.
struct NavCubeConfig {
    /// Camera orientation. Use the same Camera3D as the 3D plots in the
    /// axes — only the view rotation is used (no translation/projection).
    Camera3D camera{};
    /// Which corner of the axes rect to draw in.
    NavCubeCorner corner = NavCubeCorner::UpperLeft;
    /// Triad arrows or wireframe cube.
    NavCubeMode mode = NavCubeMode::Triad;
    /// Radius of the indicator in pixels (center to axis tip).
    float size = 36.0f;
    /// Margin from the axes rect edges in pixels.
    float margin = 12.0f;
    /// Draw "x"/"y"/"z" labels at the axis tips.
    bool showLabels = true;
    /// Draw the negative half-axes (dimmer) in Triad mode.
    bool showNegativeAxes = true;
    /// Axis colors (matplotlib convention: x=red, y=green, z=blue).
    Color xColor = Color::fromRgba8(214, 39, 40);
    Color yColor = Color::fromRgba8(44, 160, 44);
    Color zColor = Color::fromRgba8(31, 119, 180);
    /// Color for negative half-axes and cube edges.
    Color dimColor = Color::fromRgba8(160, 160, 160, 200);
    /// Line width for axis arrows.
    float axisWidth = 2.0f;
};

/// NavCubePlot — 3D navigation cube / axis triad overlay.
///
/// Draws a small orientation indicator in a corner of the axes rect,
/// showing the current Camera3D view rotation. Equivalent to the
/// orientation widget in Blender/Paraview (matplotlib 3.10+ has a
/// similar `ax3d` orientation indicator).
///
/// The indicator is an overlay: it does not contribute to autoscale and
/// does not use the axes viewport. Axis directions are projected through
/// the camera's view rotation only (orthographic), so the widget mirrors
/// the 3D scene orientation.
///
/// Usage:
///   NavCubeConfig nav;
///   nav.camera = myCamera;  // same camera as the 3D plots
///   axes->addPlot(std::make_unique<NavCubePlot>(nav));
class NavCubePlot : public IPlot {
public:
    [[nodiscard]] bool is3D() const override { return true; }
    explicit NavCubePlot(NavCubeConfig config = {});

    /// Update the camera (e.g. after interactive rotation).
    void setCamera(const Camera3D& camera) { config_.camera = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport&) const override {}

private:
    NavCubeConfig config_;
    bool prepared_ = false;
};

} // namespace volcano::plot
