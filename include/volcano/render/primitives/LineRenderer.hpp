// volcano/render/primitives/LineRenderer.hpp — polyline strip renderer
//
// Backend-neutral interface. Vulkan impl: LineRendererVk in
// src/render/primitives/LineRenderer.cpp. Instances come from
// GpuServices::createLineRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

/// Draws a polyline as a GPU line strip (plus shader join mitigation).
/// On WebGPU this is implemented as tessellated geometry; the caller
/// sees the same API either way.
class LineRenderer {
public:
    virtual ~LineRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> points,
                        plot::Color color, float width) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform,
                      uint32_t pointCount) const = 0;

    /// Opaque token for the uploaded point buffer (vec2 data) — used to
    /// bind compute-generated buffers (function eval, GPU tessellation).
    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;
    virtual void updatePoints(std::span<const plot::Point2D> points) = 0;

    /// Bind an externally-owned buffer (e.g. EvalRenderer output). The
    /// caller guarantees the buffer outlives this renderer's draws.
    virtual void bindExternalBuffer(GpuBuf buf, uint32_t count) = 0;
    /// Update color/width without re-uploading points — needed by
    /// bindExternalBuffer users (GPU-evaluated function plots) that
    /// never call upload().
    virtual void setStyle(plot::Color color, float width) = 0;
};

} // namespace volcano::render::primitives
