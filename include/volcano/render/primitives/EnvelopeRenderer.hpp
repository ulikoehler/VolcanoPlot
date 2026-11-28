// volcano/render/primitives/EnvelopeRenderer.hpp — GPU min/max envelope
//
// Backend-neutral interface. The Vulkan implementation expands one
// instanced quad per segment in the vertex shader from the uploaded
// [x, ymin, ymax, pad] records, so no CPU triangle mesh is built.
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

class EnvelopeRenderer {
public:
    virtual ~EnvelopeRenderer() = default;

    /// Upload the band records. `data` is 4 floats per point:
    /// {x, ymin, ymax, pad}. Called from IPlot::preDraw, so the write
    /// is recorded into the frame's pre-pass command buffer.
    virtual void update(Cmd& cmd, std::span<const float> data) = 0;

    /// Draw (pointCount - 1) quads, one per adjacent point pair.
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform,
                      plot::Color color) const = 0;

    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;
};

} // namespace volcano::render::primitives
