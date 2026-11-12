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
    /// output of transformPoints) — no host upload. `inBase` is the
    /// point-index offset of the polyline inside `points` (0 for a
    /// dedicated buffer). Returns {} when the backend has no
    /// device-buffer entry point.
    virtual std::vector<Mesh> tessellateDevice(Cmd& cmd, GpuBuf points,
                                               uint32_t count,
                                               uint32_t inBase,
                                               const plot::StrokeParams& sp,
                                               plot::Color color) {
        (void)cmd; (void)points; (void)count; (void)inBase; (void)sp;
        (void)color;
        return {};
    }

    /// Map data-space points to pixel space on the device: applies the
    /// closed-form axis scales + projection and masks out-of-domain
    /// points to NaN. Returns false when unavailable (caller keeps the
    /// CPU transform); the result is a device buffer, not a readback.
    /// `outBase` (optional) receives the point-index offset of the
    /// output inside `out` — pass it as tessellateDevice's `inBase`.
    /// `deferBarrier`: skip the per-dispatch RAW barrier — the caller
    /// guarantees endTransforms() runs before any consumer dispatch
    /// (the phased preDraw/preDraw2 split batches N barriers into one).
    [[nodiscard]] virtual bool transformPoints(
        Cmd& cmd, GpuBuf points, uint32_t count,
        const plot::Transform2D& t, plot::Rect2D rect, GpuBuf& out,
        uint32_t* outBase = nullptr, bool deferBarrier = false) {
        (void)cmd; (void)points; (void)count; (void)t; (void)rect;
        (void)out; (void)outBase; (void)deferBarrier;
        return false;
    }

    /// Narrow an interleaved f64 (dvec2) point buffer to f32 (vec2)
    /// on the device — records a compute dispatch into `cmd` plus the
    /// barrier making the result visible to later passes in the same
    /// command buffer. Returns false when unavailable (no fp64
    /// pipeline); the caller then falls back to CPU conversion.
    /// srcHead/srcCap describe a ring layout in src64: logical index i
    /// is read from slot (srcHead + i) % srcCap. srcCap >= n always.
    [[nodiscard]] virtual bool convertF64ToF32(Cmd& cmd, GpuBuf src64,
                                               GpuBuf dst32, uint32_t n,
                                               uint32_t srcHead = 0,
                                               uint32_t srcCap = 0) {
        (void)cmd; (void)src64; (void)dst32; (void)n;
        (void)srcHead; (void)srcCap;
        return false;
    }

    /// transformPoints reading a dvec2 (f64) source buffer directly —
    /// fuses the f64→f32 narrow into the data→pixel pass so the f32
    /// point buffer never materializes on the device. The shader
    /// narrows first, then applies the same scale/mask math, so the
    /// output is bit-identical to convertF64ToF32 + transformPoints.
    /// Returns false when unavailable (no fp64 transform pipeline);
    /// the caller then narrows separately and uses transformPoints.
    /// srcHead/srcCap: ring layout of src64 — logical index i is read
    /// from slot (srcHead + i) % srcCap (srcCap >= count always).
    [[nodiscard]] virtual bool transformPointsF64(
        Cmd& cmd, GpuBuf src64, uint32_t count,
        uint32_t srcHead, uint32_t srcCap,
        const plot::Transform2D& t, plot::Rect2D rect, GpuBuf& out,
        uint32_t* outBase = nullptr, bool deferBarrier = false) {
        (void)cmd; (void)src64; (void)count; (void)t; (void)rect;
        (void)out; (void)outBase; (void)deferBarrier;
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

    /// Opaque persistent tessellate destination owned by a plot — see
    /// tessellateDeviceTo(). Backends without persistent output regions
    /// return nullptr from createMeshStore().
    struct MeshStore {
        virtual ~MeshStore() = default;
    };

    /// Allocate a persistent mesh destination — a plot keeps it for its
    /// lifetime so a cached Mesh stays valid across frames.
    virtual std::unique_ptr<MeshStore> createMeshStore() {
        return nullptr;
    }

    /// Generation counter for the underlying vertex buffer(s): bumped
    /// whenever they regrow, which invalidates every Mesh handle cached
    /// under an older epoch. Plots must rebuild on mismatch.
    [[nodiscard]] virtual uint64_t meshEpoch() const noexcept { return 0; }

    /// tessellateDevice variant writing a caller-owned persistent
    /// region: the returned Mesh stays valid across frames until the
    /// store is rewritten or meshEpoch() bumps — the caller may skip
    /// transform+tessellate entirely while all mesh inputs (data,
    /// transform, rect, stroke, color) are unchanged. Returns {} when
    /// unsupported; the caller then falls back to tessellateDevice.
    virtual std::vector<Mesh> tessellateDeviceTo(
        Cmd& cmd, GpuBuf points, uint32_t count, uint32_t inBase,
        MeshStore& dst, const plot::StrokeParams& sp,
        plot::Color color) {
        (void)cmd; (void)points; (void)count; (void)inBase; (void)dst;
        (void)sp; (void)color;
        return {};
    }

    /// Called once between the preDraw and preDraw2 loops when
    /// transforms were recorded with deferBarrier — emits the single
    /// compute-write→compute-read barrier covering every transform
    /// this frame. Default no-op.
    virtual void endTransforms(Cmd& cmd) { (void)cmd; }

    /// Called once after all plot preDraw() calls in a frame. The
    /// Vulkan impl records the single compute→vertex barrier covering
    /// every tessellateDevice write recorded this frame (they write
    /// disjoint arena regions, so per-trace barriers are redundant).
    /// Default no-op — op-stream backends order via submission.
    virtual void endPreDraw(Cmd& cmd) { (void)cmd; }
};

} // namespace volcano::render::primitives
