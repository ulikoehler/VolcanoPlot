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

    /// Upload interleaved f64 points (dvec2 layout) into a dedicated
    /// buffer. The data is narrowed into pointBuffer() on the GPU —
    /// see GpuLineRenderer::convertF64ToF32 — so the host never runs a
    /// per-element f64→f32 conversion. Returns false when the backend
    /// has no f64 buffer path; callers then fall back to a CPU-side
    /// conversion + updatePoints.
    virtual bool updatePointsF64(std::span<const double> xy) {
        (void)xy; return false;
    }
    /// Sliding-window update: drop `drop` points from the front of the
    /// logical data range and append `tail` (interleaved f64) at the
    /// back. Implemented as a ring buffer — only the tail is memcpy'd,
    /// O(new) instead of O(total). Returns false when unsupported or
    /// when the new total exceeds the ring capacity; the caller then
    /// falls back to a full updatePointsF64 of the window.
    virtual bool updatePointsF64Append(uint32_t drop,
                                       std::span<const double> tail) {
        (void)drop; (void)tail; return false;
    }
    /// Ring state of the f64 buffer: logical index i lives at slot
    /// (head + i) % cap. cap == 0 means "linear" (head always 0).
    virtual void f64Ring(uint32_t& head, uint32_t& cap) const noexcept {
        head = 0; cap = 0;
    }
    /// Opaque token for the f64 point buffer (dvec2 data).
    [[nodiscard]] virtual GpuBuf pointBufferF64() const noexcept { return 0; }
    /// Bumped every time a point buffer is (re)allocated — consumers
    /// that cache descriptors keyed by buffer handle must invalidate
    /// on change, since a freed VkBuffer handle can be recycled for a
    /// different buffer.
    [[nodiscard]] virtual uint64_t bufEpoch() const noexcept { return 0; }
    /// True while an f64 upload is awaiting the GPU-side narrowing pass.
    [[nodiscard]] virtual bool f64Pending() const noexcept { return false; }
    virtual void clearF64Pending() noexcept {}

    /// Bind an externally-owned buffer (e.g. EvalRenderer output). The
    /// caller guarantees the buffer outlives this renderer's draws.
    virtual void bindExternalBuffer(GpuBuf buf, uint32_t count) = 0;
    /// Update color/width without re-uploading points — needed by
    /// bindExternalBuffer users (GPU-evaluated function plots) that
    /// never call upload().
    virtual void setStyle(plot::Color color, float width) = 0;
};

} // namespace volcano::render::primitives
