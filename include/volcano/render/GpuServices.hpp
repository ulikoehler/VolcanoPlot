// volcano/render/GpuServices.hpp — backend-neutral GPU service factory
//
// The single seam between the backend-neutral plot/layout engine and a
// concrete GPU backend. The Vulkan implementation is VulkanGpuServices
// (src/render/VulkanGpuServices.hpp); the WebGPU/WASM implementation is
// web::OpGpuServices (src/web/) which records every call into an
// OpStream consumed by the TypeScript interpreter.
//
// Lifetime: one GpuServices per backend, owned by the backend and
// reachable through IBackend::gpu() / Renderer::gpu(). Primitive
// instances it returns stay valid until the services are destroyed
// (per-plot unique_ptrs) or the next beginFrameScratch() (shared
// scratch state only — the objects themselves persist).
#pragma once

#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace volcano::text { class TextRenderer; }

namespace volcano::render {
class Grid3DRenderer;
}

namespace volcano::render::primitives {
class SpineRenderer;
class PointRenderer;
class LineRenderer;
class LineSegmentRenderer;
class FillRenderer;
class BarRenderer;
class PieRenderer;
class HeatmapRenderer;
class SurfaceRenderer;
class InstancedPathRenderer;
class GpuLineRenderer;
class ReduceRenderer;
class EvalRenderer;
class KdeEvalRenderer;
}

namespace volcano::render {

/// Description of a service-allocated GPU buffer.
struct GpuBufferDesc {
    uint64_t size = 0;
    bool vertex = false;      ///< bound as vertex input
    bool storage = false;     ///< compute shader read/write
    bool index = false;       ///< bound as index input
    bool uniform = false;     ///< bound as uniform
    bool hostVisible = false; ///< CPU-writable (staging/update)
};

/// Backend-neutral handle to a text face (impl detail lives in the
/// text engine — see text/TextRenderer.hpp).
class GpuServices {
public:
    virtual ~GpuServices() = default;

    /// Framebuffer extent in pixels — the draw-coordinate space.
    [[nodiscard]] virtual plot::Extent2D extent() const = 0;

    // ── Shared renderers ─────────────────────────────────────────────
    // One instance per services object, reused frame to frame. Created
    // lazily on first access.
    virtual primitives::SpineRenderer& spine() = 0;
    virtual primitives::PointRenderer& sharedPoints() = 0;
    virtual primitives::InstancedPathRenderer& instancedPath() = 0;
    virtual primitives::GpuLineRenderer& gpuLine() = 0;
    virtual primitives::ReduceRenderer& reduce() = 0;
    virtual primitives::KdeEvalRenderer& kdeEval() = 0;
    virtual Grid3DRenderer& grid3D() = 0;
    virtual text::TextRenderer& text() = 0;

    // ── Per-plot factories ───────────────────────────────────────────
    // Fresh instance per call; the caller owns the unique_ptr.
    virtual std::unique_ptr<primitives::PointRenderer>
        createPointRenderer() = 0;
    virtual std::unique_ptr<primitives::LineRenderer>
        createLineRenderer() = 0;
    virtual std::unique_ptr<primitives::LineSegmentRenderer>
        createLineSegmentRenderer() = 0;
    virtual std::unique_ptr<primitives::FillRenderer>
        createFillRenderer() = 0;
    virtual std::unique_ptr<primitives::BarRenderer>
        createBarRenderer() = 0;
    virtual std::unique_ptr<primitives::PieRenderer>
        createPieRenderer() = 0;
    virtual std::unique_ptr<primitives::HeatmapRenderer>
        createHeatmapRenderer() = 0;
    virtual std::unique_ptr<primitives::SurfaceRenderer>
        createSurfaceRenderer() = 0;
    virtual std::unique_ptr<primitives::InstancedPathRenderer>
        createInstancedPathRenderer() = 0;
    virtual std::unique_ptr<primitives::EvalRenderer>
        createEvalRenderer() = 0;

    // ── Buffers ──────────────────────────────────────────────────────
    /// Allocate a buffer owned by the services. The token stays valid
    /// until destroyBuffer() or services teardown.
    virtual GpuBuf createBuffer(const GpuBufferDesc& desc) = 0;
    /// Upload bytes into a service buffer (async-capable: maps to
    /// GPUQueue.writeBuffer on WebGPU, host-visible memcpy on Vulkan).
    virtual void writeBuffer(GpuBuf buf, uint64_t offset,
                             std::span<const std::byte> data) = 0;
    virtual void destroyBuffer(GpuBuf buf) = 0;

    // ── Frame lifecycle ──────────────────────────────────────────────
    /// Reset per-frame scratch state on all shared renderers
    /// (scratch offsets, retired-buffer release).
    virtual void beginFrameScratch() = 0;

    /// Begin the GPU pre-pass (compute work feeding vertex input, e.g.
    /// GpuLineRenderer::tessellate). Returns a Cmd the caller passes to
    /// IPlot::preDraw; submitPrePass() ends + submits it. On the op
    /// backend this wraps the same OpStream (ops recorded in order).
    virtual std::unique_ptr<Cmd> beginPrePass() = 0;
    virtual void submitPrePass(std::unique_ptr<Cmd> cmd) = 0;

    // ── Text atlas ───────────────────────────────────────────────────
    /// Ensure the text renderer's GPU half is initialized and its atlas
    /// uploaded. Idempotent.
    virtual void ensureText() = 0;
    /// Re-upload the glyph atlas after the CPU-side bitmap grew
    /// (TextRenderer::atlasDirty()).
    virtual void syncTextAtlas() = 0;

    // ── Bespoke compute ──────────────────────────────────────────────
    // Synchronous results on Vulkan; the op backend returns failure and
    // the caller takes its CPU path (or skips — v1 feature gaps).

    /// Histogram binning compute: data → nBins counts over [lo, hi].
    virtual std::optional<std::vector<uint32_t>> histBin(
        std::span<const float> data, uint32_t nBins, float lo, float hi) {
        (void)data; (void)nBins; (void)lo; (void)hi;
        return std::nullopt;
    }

    /// Violin density normalization compute (sample → normalized KDE).
    virtual std::optional<std::vector<float>> violinNormalize(
        std::span<const float> src, uint32_t n) {
        (void)src; (void)n;
        return std::nullopt;
    }

    /// Pcolormesh quad tessellation compute: cell coords (x,y) + scalar
    /// field t colormapped via lut → per-vertex posOut/colOut buffers.
    /// `cells` is the number of quad cells; outputs hold 6 verts/cell.
    virtual bool pcmTessellate(GpuBuf x, GpuBuf y, GpuBuf t, GpuBuf lut,
                               uint32_t lutSize, GpuBuf posOut,
                               GpuBuf colOut, uint32_t cells) {
        (void)x; (void)y; (void)t; (void)lut; (void)lutSize;
        (void)posOut; (void)colOut; (void)cells;
        return false;
    }
};

} // namespace volcano::render
