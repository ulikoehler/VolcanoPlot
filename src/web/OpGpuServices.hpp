// src/web/OpGpuServices.hpp — GpuServices over an OpStream (VOLCANO_WEB)
//
// All resources are u32 op-stream handles packed into GpuBuf tokens.
// createBuffer/writeBuffer emit ops into the *current* frame stream —
// impls call createBufferRaw/writeBufferRaw which stash deferred
// resource ops that the next frame's OpCmd picks up (uploads happen at
// IPlot::prepare time, outside any Cmd).
#pragma once

#include "OpStream.hpp"
#include <volcano/render/GpuServices.hpp>
#include <volcano/text/TextRenderer.hpp>
// unique_ptr members need complete types (destructor instantiated
// where OpGpuServices is created — e.g. WebBackend.hpp).
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/render/primitives/PointRenderer.hpp>
#include <volcano/render/primitives/LineRenderer.hpp>
#include <volcano/render/primitives/LineSegmentRenderer.hpp>
#include <volcano/render/primitives/FillRenderer.hpp>
#include <volcano/render/primitives/BarRenderer.hpp>
#include <volcano/render/primitives/PieRenderer.hpp>
#include <volcano/render/primitives/HeatmapRenderer.hpp>
#include <volcano/render/primitives/SurfaceRenderer.hpp>
#include <volcano/render/primitives/InstancedPathRenderer.hpp>
#include <volcano/render/primitives/GpuLineRenderer.hpp>
#include <volcano/render/primitives/ReduceRenderer.hpp>
#include <volcano/render/primitives/EvalRenderer.hpp>
#include <volcano/render/primitives/KdeEvalRenderer.hpp>
#include <volcano/render/Grid3DRenderer.hpp>

#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace volcano::web {

class OpGpuServices final : public render::GpuServices {
public:
    OpGpuServices() = default;

    /// Session-long stream: resource ops (uploads during prepare) and
    /// frame ops land in the same ordered stream — the interpreter
    /// executes resource ops during the walk (§2 ordering). The backend
    /// calls resetFrame() at frame start, finish() at frame end.
    OpStream& stream() { return stream_; }
    /// Non-null stream pointer for impls (always bound — kept for call
    /// symmetry with backends that may record lazily).
    OpStream* curStream() { return &stream_; }
    /// Clear the record/arena sections (handles stay valid — resource
    /// ops from earlier frames already executed on the JS side).
        /// Per-frame scratch reset. Does NOT clear the stream — resource ops
    /// recorded outside a frame (prepare-phase uploads) must reach the
    /// next finish(); OpStream::finish() clears records after packing.
    void resetFrame() { beginFrameScratch(); }

    // ── internal helpers used by Op* impls ───────────────────────────
    /// Emit CreateBuffer (deferred when mid-stream absent) → handle.
    uint32_t createBufferRaw(uint64_t size, uint8_t kind);
    /// Emit WriteBuffer with HEAP or arena-sourced data.
    void writeBufferRaw(uint32_t handle, uint64_t off,
                        const void* data, size_t bytes);

    // ── mailbox (async compute results from JS) ──────────────────────
    /// Allocate a mailbox slot for an async GPU result.
    uint32_t allocMailbox() { return nextSlot_++; }
    /// Route a delivered mailbox value (bindings::_vp_mailbox) to the
    /// buffer that requested it.
    void deliverMailbox(uint32_t slot,
                        float v0, float v1, float v2, float v3);
    /// Latest reduce result for a point-buffer handle, if delivered.
    const render::primitives::MinMax2D*
        reduceResult(uint32_t buf) const;
    /// Record that `slot` carries the min/max of point-buffer `buf`.
    void trackReduceSlot(uint32_t slot, uint32_t buf);

    // ── bulk mailbox (arbitrary-size async results, e.g. KDE grids) ──
    /// Allocate `bytes` for `slot` and return its WASM-heap address —
    /// JS writes the readback bytes there, then calls mailboxDone.
    uintptr_t mailboxDest(uint32_t slot, uint32_t bytes);
    void mailboxDone(uint32_t slot);
    /// True once JS signalled delivery for `slot`.
    bool mailboxReady(uint32_t slot) const;
    /// Move delivered bytes out; empty when absent/not ready.
    std::vector<uint8_t> mailboxTake(uint32_t slot);
    void releaseBuffer(uint32_t handle);
    void releaseTexture(uint32_t handle);
    uint32_t createTextureRaw(uint32_t w, uint32_t h, uint8_t fmt);
    void writeTextureRaw(uint32_t handle, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h,
                         const void* data, size_t bytes);
    [[nodiscard]] uint32_t allocHandle() { return stream_.allocHandle(); }

    // ── GpuServices ──────────────────────────────────────────────────
    [[nodiscard]] plot::Extent2D extent() const override { return extent_; }
    void setExtent(plot::Extent2D e) { extent_ = e; }

    void ensureGraphics() override {}
    [[nodiscard]] bool textReady() const noexcept override {
        return textReady_;
    }

    render::primitives::SpineRenderer& spine() override;
    render::primitives::PointRenderer& sharedPoints() override;
    render::primitives::InstancedPathRenderer& instancedPath() override;
    render::primitives::GpuLineRenderer& gpuLine() override;
    render::primitives::ReduceRenderer& reduce() override;
    render::primitives::KdeEvalRenderer& kdeEval() override;
    render::Grid3DRenderer& grid3D() override;
    text::TextRenderer& text() override;

    std::unique_ptr<render::primitives::PointRenderer>
        createPointRenderer() override;
    std::unique_ptr<render::primitives::LineRenderer>
        createLineRenderer() override;
    std::unique_ptr<render::primitives::LineSegmentRenderer>
        createLineSegmentRenderer() override;
    std::unique_ptr<render::primitives::FillRenderer>
        createFillRenderer() override;
    std::unique_ptr<render::primitives::BarRenderer>
        createBarRenderer() override;
    std::unique_ptr<render::primitives::PieRenderer>
        createPieRenderer() override;
    std::unique_ptr<render::primitives::HeatmapRenderer>
        createHeatmapRenderer() override;
    std::unique_ptr<render::primitives::SurfaceRenderer>
        createSurfaceRenderer() override;
    std::unique_ptr<render::primitives::InstancedPathRenderer>
        createInstancedPathRenderer() override;
    std::unique_ptr<render::primitives::EvalRenderer>
        createEvalRenderer() override;
    std::unique_ptr<render::primitives::SpineRenderer>
        createSpineRenderer() override;
    std::unique_ptr<render::primitives::GpuLineRenderer>
        createGpuLineRenderer() override;
    std::unique_ptr<text::TextRenderer> createTextRenderer() override;

    render::GpuBuf createBuffer(const render::GpuBufferDesc& desc) override;
    void writeBuffer(render::GpuBuf buf, uint64_t offset,
                     std::span<const std::byte> data) override;
    void destroyBuffer(render::GpuBuf buf) override;
    /// Same eventual-delivery trick as OpKdeEvalRenderer: first call
    /// emits the compute + mailbox and returns nullopt (CPU covers);
    /// identical later calls serve the delivered grid.
    std::optional<std::vector<float>> kde1d(
        std::span<const float> data, float lo, float step, float bw,
        uint32_t n) override;
    /// Binning: same eventual-delivery contract — the first call emits
    /// the atomic-count compute + mailbox and returns nullopt so the
    /// caller's CPU counts cover that frame; identical later calls serve
    /// the delivered counts with no further GPU work.
    std::optional<std::vector<uint32_t>> histBin(
        std::span<const float> data, uint32_t nBins,
        float e0, float invW) override;
    std::optional<std::vector<uint32_t>> histBin2D(
        std::span<const float> x, std::span<const float> y,
        uint32_t nBinsX, uint32_t nBinsY,
        float x0, float invWX, float y0, float invWY) override;
    /// Contour tessellation: emits the marching-squares + stroking
    /// compute. The mesh never comes back to the CPU — the soup and its
    /// indirect vertex count are handed straight to the draw.
    /// Batched FFT: same eventual-delivery contract as binning — the
    /// first call emits the transform + mailbox and returns nullopt so
    /// the caller's CPU FFT covers that frame; later calls with
    /// unchanged input serve the delivered spectra.
    std::optional<std::vector<float>> fftSegments(
        std::span<const float> signal, std::span<const float> win,
        uint32_t n, uint32_t step, uint32_t numSegs) override;
    [[nodiscard]] bool supportsContourTessellate() const noexcept override {
        return true;
    }
    /// Scattered-data variant (op 53) — isoline stroking and filled
    /// band clipping share one compute op.
    [[nodiscard]] bool supportsTriContourTessellate() const noexcept override {
        return true;
    }
    bool triContourTessellate(
        std::span<const float> xyz,
        std::span<const uint32_t> tris,
        std::span<const float> levels,
        std::span<const plot::Color> colors,
        float bx, float ax, float by, float ay, float lineWidth,
        std::span<const float> dashes, uint32_t mode,
        render::GpuBuf& soupOut, render::GpuBuf& countOut) override;

    [[nodiscard]] bool supportsFillBetweenTess() const noexcept override {
        return true;
    }
    bool fillBetweenTess(std::span<const float> x,
                         std::span<const float> y1,
                         std::span<const float> y2,
                         std::span<const uint32_t> mask,
                         bool interpolate,
                         float bx, float ax, float by, float ay,
                         plot::Color color,
                         render::GpuBuf& soupOut,
                         render::GpuBuf& countOut) override;

    [[nodiscard]] bool supportsQuiverTess() const noexcept override {
        return true;
    }
    bool quiverHeads(std::span<const float> segsPx,
                     uint32_t mode, float hw2, float hl, float hal,
                     plot::Color color,
                     render::GpuBuf& soupOut,
                     render::GpuBuf& countOut) override;

    bool contourTessellate(
        std::span<const float> grid, uint32_t w, uint32_t h,
        std::span<const float> levels,
        std::span<const plot::Color> colors,
        float bx, float ax, float by, float ay, float lineWidth,
        std::span<const float> dashes,
        render::GpuBuf& soupOut, render::GpuBuf& countOut) override;
    std::optional<std::vector<uint32_t>> hexBins(
        std::span<const float> x, std::span<const float> y,
        uint32_t nx, uint32_t ny, float xMin, float yMin,
        float sx, float sy) override;
    bool pcmTessellate(std::span<const float> x,
                       std::span<const float> y,
                       std::span<const float> t,
                       std::span<const plot::Color> lut,
                       uint32_t nCols, uint32_t nRows,
                       bool gouraud, uint32_t flags,
                       render::GpuBuf& posOut,
                       render::GpuBuf& colOut) override;

    void beginFrameScratch() override;
    std::unique_ptr<render::Cmd> beginPrePass() override;
    void submitPrePass(std::unique_ptr<render::Cmd> cmd) override;

    void ensureText() override;
    void syncTextAtlas() override;

private:
    plot::Extent2D extent_{};
    OpStream stream_;
    bool textReady_ = false;
    std::unordered_set<uint32_t> live_;

    std::unique_ptr<render::primitives::SpineRenderer> spine_;
    std::unique_ptr<render::primitives::PointRenderer> points_;
    std::unique_ptr<render::primitives::InstancedPathRenderer> instPath_;
    std::unique_ptr<render::primitives::GpuLineRenderer> gpuLine_;
    std::unique_ptr<render::primitives::ReduceRenderer> reduce_;
    std::unique_ptr<render::primitives::KdeEvalRenderer> kdeEval_;
    std::unique_ptr<render::Grid3DRenderer> grid3D_;
    std::unique_ptr<text::TextRenderer> text_;

    uint32_t nextSlot_ = 1;
    std::unordered_map<uint32_t, uint32_t> slotBuf_;
    struct BulkMail { std::vector<uint8_t> bytes; bool ready = false; };
    std::unordered_map<uint32_t, BulkMail> bulkMail_;
    struct Kde1dReq {
        size_t n; float lo, step, bw; uint32_t ne; float fp;
        uint32_t slot; std::vector<float> cached;
    };
    std::vector<Kde1dReq> kdeReqs_;

    /// One in-flight/recently-delivered binning request. Binning is
    /// data-parallel and idempotent per input fingerprint, so results
    /// are cached per (kind, fingerprint) — a re-render with unchanged
    /// data reuses the delivered counts with no further GPU work.
    struct BinReq {
        uint32_t kind;      ///< 0 hist1d, 1 hist2d, 2 hexbin
        uint64_t fp;        ///< input fingerprint
        uint32_t slot;
        uint32_t count;     ///< number of u32 counts expected
        std::vector<uint32_t> cached;
    };
    std::vector<BinReq> binReqs_;
    /// State of one binning request.
    struct BinState {
        const std::vector<uint32_t>* cached = nullptr;  ///< delivered
        uint32_t slot = 0;   ///< mailbox slot to emit with
        bool isNew = false;  ///< true when this call created the request
    };
    /// Look up (kind, fp); creates the request (allocating a slot) when
    /// unknown. `cached` is set once JS delivers the counts.
    BinState binState(uint32_t kind, uint64_t fp, uint32_t count);
    std::unordered_map<uint32_t, render::primitives::MinMax2D>
        reduceResults_;
};

} // namespace volcano::web
