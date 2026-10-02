// volcano/render/primitives/LineSegmentRenderer.hpp — segment list renderer
//
// Backend-neutral interface (Vulkan impl: LineSegmentRendererVk).
// Instances come from GpuServices::createLineSegmentRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

/// Draws independent line segments (two vertices each) with per-draw
/// line type. On Vulkan this uses the line-list topology; on WebGPU the
/// interpreter tessellates since line width is fixed at 1 px there.
class LineSegmentRenderer {
public:
    virtual ~LineSegmentRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> points,
                        plot::Color color, float width) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform,
                      uint32_t vertexCount) const = 0;

    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;
};

} // namespace volcano::render::primitives
