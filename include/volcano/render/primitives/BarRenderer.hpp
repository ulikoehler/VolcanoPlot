// volcano/render/primitives/BarRenderer.hpp — bar chart renderer
//
// Backend-neutral interface (Vulkan impl: BarRendererVk).
// Instances come from GpuServices::createBarRenderer().
#pragma once

#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

namespace volcano::render::primitives {

/// Draws bar chart rectangles (per-vertex colors from BarData).
class BarRenderer {
public:
    virtual ~BarRenderer() = default;

    virtual void upload(const plot::BarData& data) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform) const = 0;
};

} // namespace volcano::render::primitives
