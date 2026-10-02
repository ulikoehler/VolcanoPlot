// volcano/render/primitives/InstancedPathRenderer.hpp — instanced path fill
//
// Backend-neutral interface (Vulkan impl: InstancedPathRendererVk).
// Instances come from GpuServices::createInstancedPathRenderer().
#pragma once

#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

/// Per-instance attributes for instanced path rendering.
struct PathInstance {
    float ox, oy;       ///< instance origin, pixel space (Y-down)
    float sx, sy;       ///< template→pixel scale (sy may be negative)
    float r, g, b, a;   ///< fill color
};

/// Draws many copies of a template path (triangle soup) at per-instance
/// offsets/scales/colors in a single instanced draw call. Used by
/// PathCollection to keep large collections (the machinery behind
/// scatter) off the per-item CPU tessellation path.
class InstancedPathRenderer {
public:
    virtual ~InstancedPathRenderer() = default;

    [[nodiscard]] virtual bool inited() const noexcept = 0;
    [[nodiscard]] virtual uint32_t templateVertCount() const noexcept = 0;

    /// Upload the template triangle soup (template space, unit-ish).
    virtual void setTemplate(std::span<const plot::Point2D> triVerts) = 0;

    /// One instanced draw of the template at each instance transform.
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent.
    virtual void drawInstanced(Cmd& cmd, plot::Rect2D clip,
                               plot::Extent2D resolution,
                               std::span<const PathInstance> instances) = 0;

    /// Per-frame scratch release (retired buffers/templates).
    virtual void resetScratch() = 0;
};

} // namespace volcano::render::primitives
