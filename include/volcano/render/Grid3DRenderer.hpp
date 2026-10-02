// volcano/render/Grid3DRenderer.hpp — fwidth-based 3D dynamic grid
//
// Backend-neutral interface (Vulkan impl: Grid3DRendererVk).
// The shared instance comes from GpuServices::grid3D().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

namespace volcano::render {

/// Configuration for the 3D grid.
struct Grid3DStyle {
    /// Grid line color.
    plot::Color color = plot::Color::fromRgba8(200, 200, 200, 150);
    /// Whether to draw the X-Z floor grid (constant Y).
    bool floorXZ = true;
    /// Whether to draw the X-Y back wall grid (constant Z).
    bool backWallXY = true;
    /// Whether to draw the Y-Z side wall grid (constant X).
    bool sideWallYZ = true;
    /// Grid line step (world units). If <= 0, auto-computed.
    float step = 0.0f;
};

/// Renders a 3D dynamic grid using screen-space derivatives (fwidth).
///
/// The grid is drawn on the floor (X-Z plane) and optionally on the back
/// and side walls of the 3D axes box. Grid lines are computed in the
/// fragment shader using fwidth for anti-aliasing, so they never quantize
/// under zoom — the same technique as the 2D grid approach.
///
/// The renderer uses a fullscreen triangle and ray-casts into the 3D
/// scene to find the intersection with the floor/wall planes, then
/// computes grid line distances in world space.
class Grid3DRenderer {
public:
    virtual ~Grid3DRenderer() = default;

    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Viewport& viewport,
                      const plot::Camera3D& camera,
                      const Grid3DStyle& style) const = 0;
};

} // namespace volcano::render
