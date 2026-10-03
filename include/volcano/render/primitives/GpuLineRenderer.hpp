// volcano/render/primitives/GpuLineRenderer.hpp — compute polyline stroker
//
// Backend-neutral interface (Vulkan impl: GpuLineRendererVk).
// The shared instance comes from GpuServices::gpuLine().
#pragma once

#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace volcano::render::primitives {

/// Compute-side polyline tessellation: strokes a pixel-space polyline
/// into a LineVertex triangle soup entirely on the GPU. Used by LinePlot
/// when the point count makes CPU stroking the bottleneck.
class GpuLineRenderer {
public:
    virtual ~GpuLineRenderer() = default;

    /// A GPU-resident triangle mesh produced by tessellate().
    struct Mesh {
        GpuBuf buffer = 0;
        uint64_t firstVertex = 0;   ///< byte offset of the first vertex
        uint32_t vertexCount = 0;
    };

    /// Record (Vulkan) or emit (op stream) the compute pass that strokes
    /// `px` into triangle meshes. Returns one Mesh per emitted soup —
    /// consumed by SpineRenderer::drawTrianglesGpu in draw().
    virtual std::vector<Mesh> tessellate(Cmd& cmd,
                                         std::span<const plot::Point2D> px,
                                         const plot::StrokeParams& sp,
                                         plot::Color color) = 0;

    /// Stroke a polyline that already lives in a device buffer (e.g. the
    /// output of transformPoints) — no host upload. Returns {} when the
    /// backend has no device-buffer entry point.
    virtual std::vector<Mesh> tessellateDevice(Cmd& cmd, GpuBuf points,
                                               uint32_t count,
                                               const plot::StrokeParams& sp,
                                               plot::Color color) {
        (void)cmd; (void)points; (void)count; (void)sp; (void)color;
        return {};
    }

    /// Map data-space points to pixel space on the device: applies the
    /// closed-form axis scales + projection and masks out-of-domain
    /// points to NaN. Returns false when unavailable (caller keeps the
    /// CPU transform); the result is a device buffer, not a readback.
    [[nodiscard]] virtual bool transformPoints(
        Cmd& cmd, GpuBuf points, uint32_t count,
        const plot::Transform2D& t, plot::Rect2D rect, GpuBuf& out) {
        (void)cmd; (void)points; (void)count; (void)t; (void)rect; (void)out;
        return false;
    }

    /// Column-wise min/max envelope over a point buffer (compute).
    /// Returns false when unavailable — callers fall back to CPU.
    /// On the op-stream backend the result arrives via a mailbox next
    /// frame; the v1 implementation returns false.
    [[nodiscard]] virtual bool envelopeColumns(GpuBuf points,
                                               uint32_t count,
                                               float ax, float kx,
                                               int cx0, int cx1,
                                               std::vector<float>& mn,
                                               std::vector<float>& mx) = 0;

    /// True when tessellate() honours `StrokeParams::dashes`. The
    /// Vulkan backend strokes solid polylines only, so dashed strokes
    /// must stay on the CPU stroker there.
    [[nodiscard]] virtual bool supportsDashes() const noexcept {
        return false;
    }

    virtual void resetScratch() = 0;
    [[nodiscard]] virtual bool inited() const noexcept = 0;
};

} // namespace volcano::render::primitives
