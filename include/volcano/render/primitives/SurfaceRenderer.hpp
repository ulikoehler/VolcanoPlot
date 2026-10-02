// volcano/render/primitives/SurfaceRenderer.hpp — 3D surface renderer
//
// Backend-neutral interface (Vulkan impl: SurfaceRendererVk).
// Instances come from GpuServices::createSurfaceRenderer().
#pragma once

#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

namespace volcano::render::primitives {

/// Draws a height-field surface with lighting and depth testing.
class SurfaceRenderer {
public:
    virtual ~SurfaceRenderer() = default;

    virtual void upload(const plot::Grid2D& grid) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Camera3D& camera, bool shade = true,
                      float lightAzdeg = 315.0f,
                      float lightAltdeg = 45.0f) const = 0;
};

} // namespace volcano::render::primitives
