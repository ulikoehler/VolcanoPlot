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

    /// Idempotent lazy init of the shared graphics renderers (the
    /// services may compile pipelines on first call).
    virtual void ensureGraphics() = 0;
    /// True once the text renderer's atlas is uploaded and usable.
    [[nodiscard]] virtual bool textReady() const noexcept = 0;

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
    virtual std::unique_ptr<primitives::SpineRenderer>
        createSpineRenderer() = 0;
    virtual std::unique_ptr<primitives::GpuLineRenderer>
        createGpuLineRenderer() = 0;
    virtual std::unique_ptr<text::TextRenderer>
        createTextRenderer() = 0;

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

    /// Histogram binning compute: data → nBins counts, bin index
    /// floor((v - e0) * invW). Returns nullopt when unavailable.
    virtual std::optional<std::vector<uint32_t>> histBin(
        std::span<const float> data, uint32_t nBins,
        float e0, float invW) {
        (void)data; (void)nBins; (void)e0; (void)invW;
        return std::nullopt;
    }

    /// 2-D histogram binning with uniform edges: x/y sample pairs →
    /// nBinsX*nBinsY counts, row-major (y-major). Nullopt when the
    /// backend has no GPU path or the result is not ready yet (the
    /// caller then keeps its CPU counts for this frame).
    virtual std::optional<std::vector<uint32_t>> histBin2D(
        std::span<const float> x, std::span<const float> y,
        uint32_t nBinsX, uint32_t nBinsY,
        float x0, float invWX, float y0, float invWY) {
        (void)x; (void)y; (void)nBinsX; (void)nBinsY;
        (void)x0; (void)invWX; (void)y0; (void)invWY;
        return std::nullopt;
    }

    /// Grid contour tessellation on the device: marching squares over
    /// `grid` for every `levels[i]`, stroke-expanded into a triangle
    /// soup of {vec2 pos_px, vec4 rgba} records plus an indirect vertex
    /// count. Returns false when the backend has no GPU path — the
    /// caller then runs its CPU marching-squares/stroker.
    ///
    /// `bx/ax/by/ay` are the data→pixel affine (px = b + v*a) for the
    /// axes rect; `colors[i]` is the level colour; `dashes` holds
    /// (on, off) per level — `on == 0` draws that level solid.
    virtual bool contourTessellate(
        std::span<const float> grid, uint32_t w, uint32_t h,
        std::span<const float> levels,
        std::span<const plot::Color> colors,
        float bx, float ax, float by, float ay, float lineWidth,
        std::span<const float> dashes,
        GpuBuf& soupOut, GpuBuf& countOut) {
        (void)grid; (void)w; (void)h; (void)levels; (void)colors;
        (void)bx; (void)ax; (void)by; (void)ay; (void)lineWidth;
        (void)dashes; (void)soupOut; (void)countOut;
        return false;
    }

    /// Batched real-input FFT for the spectrum family: `numSegs` windows
    /// of `n` samples (hop `step`) taken from `signal`, multiplied by the
    /// `n`-sample window `win`, transformed. Returns `numSegs * n`
    /// complex values interleaved (re, im), or nullopt when the backend
    /// has no GPU path / the result is not ready yet (the caller keeps
    /// its CPU transform for that frame).
    virtual std::optional<std::vector<float>> fftSegments(
        std::span<const float> signal, std::span<const float> win,
        uint32_t n, uint32_t step, uint32_t numSegs) {
        (void)signal; (void)win; (void)n; (void)step; (void)numSegs;
        return std::nullopt;
    }

    /// True when this backend implements contourTessellate (checked
    /// before the plot skips its CPU marching squares).
    [[nodiscard]] virtual bool supportsContourTessellate() const noexcept {
        return false;
    }

    /// mpl hexbin (pointy-top) lattice counts: lattice A of
    /// (nx+1)*(ny+1) cells followed by lattice B of nx*ny cells.
    /// Nullopt when unavailable (see histBin2D).
    virtual std::optional<std::vector<uint32_t>> hexBins(
        std::span<const float> x, std::span<const float> y,
        uint32_t nx, uint32_t ny, float xMin, float yMin,
        float sx, float sy) {
        (void)x; (void)y; (void)nx; (void)ny;
        (void)xMin; (void)yMin; (void)sx; (void)sy;
        return std::nullopt;
    }

    /// 1-D Gaussian KDE over `data` evaluated at `n` points from `lo`
    /// with spacing `step` and bandwidth `bw`. Nullopt when unavailable.
    virtual std::optional<std::vector<float>> kde1d(
        std::span<const float> data, float lo, float step, float bw,
        uint32_t n) {
        (void)data; (void)lo; (void)step; (void)bw; (void)n;
        return std::nullopt;
    }

    /// Pcolormesh quad tessellation compute: cell coords (x,y edges) +
    /// normalized scalar field `t` colormapped via `lut` → adopted
    /// per-vertex position/color buffers (6 verts/cell flat, 12 for
    /// gouraud). `flags`: bit0 = cmap.bad present, bit1 = skip NaN.
    /// Outputs are returned as tokens consumable by
    /// FillRenderer::adoptBuffers.
    virtual bool pcmTessellate(std::span<const float> x,
                               std::span<const float> y,
                               std::span<const float> t,
                               std::span<const plot::Color> lut,
                               uint32_t nCols, uint32_t nRows,
                               bool gouraud, uint32_t flags,
                               GpuBuf& posOut, GpuBuf& colOut) {
        (void)x; (void)y; (void)t; (void)lut; (void)nCols; (void)nRows;
        (void)gouraud; (void)flags; (void)posOut; (void)colOut;
        return false;
    }
};

} // namespace volcano::render
