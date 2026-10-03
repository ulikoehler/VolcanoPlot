// src/web/OpGpuServices.cpp — GpuServices over an OpStream
#include "OpGpuServices.hpp"
#include <cmath>
#include "OpFactory.hpp"
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

#include "OpCmd.hpp"
#include "OpPayloads.hpp"
#include <volcano/render/Grid3DRenderer.hpp>

namespace volcano::web {

// ── raw resource helpers ─────────────────────────────────────────────

uint32_t OpGpuServices::createBufferRaw(uint64_t size, uint8_t kind) {
    uint32_t h = allocHandle();
    PCreateBuffer p{h, size, kind};
    stream_.emit(Op::CreateBuffer, p);
    live_.insert(h);
    return h;
}
void OpGpuServices::writeBufferRaw(uint32_t handle, uint64_t off,
                                   const void* data, size_t bytes) {
    PWriteBuffer p{handle, off, stream_.arenaCopy(data, bytes)};
    stream_.emit(Op::WriteBuffer, p);
}
void OpGpuServices::releaseBuffer(uint32_t handle) {
    if (!live_.erase(handle)) return;
    PReleaseBuffer p{handle};
    stream_.emit(Op::ReleaseBuffer, p);
}
uint32_t OpGpuServices::createTextureRaw(uint32_t w, uint32_t h,
                                         uint8_t fmt) {
    uint32_t t = allocHandle();
    PCreateTexture p{t, w, h, fmt};
    stream_.emit(Op::CreateTexture, p);
    live_.insert(t);
    return t;
}
void OpGpuServices::writeTextureRaw(uint32_t handle, uint32_t x,
                                    uint32_t y, uint32_t w, uint32_t h,
                                    const void* data, size_t bytes) {
    PWriteTexture p{handle, x, y, w, h,
                    stream_.arenaCopy(data, bytes)};
    stream_.emit(Op::WriteTexture, p);
}
void OpGpuServices::releaseTexture(uint32_t handle) {
    if (!live_.erase(handle)) return;
    PReleaseTexture p{handle};
    stream_.emit(Op::ReleaseTexture, p);
}

// ── GpuServices ──────────────────────────────────────────────────────

render::primitives::SpineRenderer& OpGpuServices::spine() {
    if (!spine_) spine_ = op::makeSpine(*this);
    return *spine_;
}
render::primitives::PointRenderer& OpGpuServices::sharedPoints() {
    if (!points_) points_ = op::makePoint(*this);
    return *points_;
}
render::primitives::InstancedPathRenderer& OpGpuServices::instancedPath() {
    if (!instPath_) instPath_ = op::makeInstancedPath(*this);
    return *instPath_;
}
render::primitives::GpuLineRenderer& OpGpuServices::gpuLine() {
    if (!gpuLine_) gpuLine_ = op::makeGpuLine(*this);
    return *gpuLine_;
}
render::primitives::ReduceRenderer& OpGpuServices::reduce() {
    if (!reduce_) reduce_ = op::makeReduce(*this);
    return *reduce_;
}
render::primitives::KdeEvalRenderer& OpGpuServices::kdeEval() {
    if (!kdeEval_) kdeEval_ = op::makeKdeEval(*this);
    return *kdeEval_;
}
render::Grid3DRenderer& OpGpuServices::grid3D() {
    if (!grid3D_) grid3D_ = op::makeGrid3D(*this);
    return *grid3D_;
}
text::TextRenderer& OpGpuServices::text() {
    ensureText();
    return *text_;
}

std::unique_ptr<render::primitives::PointRenderer>
OpGpuServices::createPointRenderer() { return op::makePoint(*this); }
std::unique_ptr<render::primitives::LineRenderer>
OpGpuServices::createLineRenderer() { return op::makeLine(*this); }
std::unique_ptr<render::primitives::LineSegmentRenderer>
OpGpuServices::createLineSegmentRenderer() {
    return op::makeLineSegment(*this);
}
std::unique_ptr<render::primitives::FillRenderer>
OpGpuServices::createFillRenderer() { return op::makeFill(*this); }
std::unique_ptr<render::primitives::BarRenderer>
OpGpuServices::createBarRenderer() { return op::makeBar(*this); }
std::unique_ptr<render::primitives::PieRenderer>
OpGpuServices::createPieRenderer() { return op::makePie(*this); }
std::unique_ptr<render::primitives::HeatmapRenderer>
OpGpuServices::createHeatmapRenderer() { return op::makeHeatmap(*this); }
std::unique_ptr<render::primitives::SurfaceRenderer>
OpGpuServices::createSurfaceRenderer() { return op::makeSurface(*this); }
std::unique_ptr<render::primitives::InstancedPathRenderer>
OpGpuServices::createInstancedPathRenderer() {
    return op::makeInstancedPath(*this);
}
std::unique_ptr<render::primitives::EvalRenderer>
OpGpuServices::createEvalRenderer() { return op::makeEval(*this); }
std::unique_ptr<render::primitives::SpineRenderer>
OpGpuServices::createSpineRenderer() { return op::makeSpine(*this); }
std::unique_ptr<render::primitives::GpuLineRenderer>
OpGpuServices::createGpuLineRenderer() { return op::makeGpuLine(*this); }
std::unique_ptr<text::TextRenderer>
OpGpuServices::createTextRenderer() { return op::makeText(*this); }

render::GpuBuf
OpGpuServices::createBuffer(const render::GpuBufferDesc& desc) {
    // Usage bitmask: bit0 vertex, bit1 storage, bit2 index,
    // bit3 uniform, bit4 hostVisible (mirrors GpuBufferDesc).
    uint8_t kind = uint8_t((desc.vertex ? 1 : 0) | (desc.storage ? 2 : 0)
                         | (desc.index ? 4 : 0) | (desc.uniform ? 8 : 0)
                         | (desc.hostVisible ? 16 : 0));
    return render::GpuBuf(createBufferRaw(desc.size, kind));
}
void OpGpuServices::writeBuffer(render::GpuBuf buf, uint64_t offset,
                                std::span<const std::byte> data) {
    writeBufferRaw(uint32_t(buf), offset, data.data(), data.size());
}
namespace {

/// Cheap content fingerprint: element count plus a strided sample of
/// the values (up to 64 probes + first/last). Binning results are
/// cached per fingerprint, so identical re-renders cost no GPU work —
/// while any real data change invalidates the cache.
uint64_t fpFloats(std::span<const float> a,
                  std::span<const float> b = {}) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    auto sample = [&mix](std::span<const float> v) {
        mix(v.size());
        if (v.empty()) return;
        const size_t step = std::max<size_t>(1, v.size() / 64);
        for (size_t i = 0; i < v.size(); i += step) {
            uint32_t bits;
            std::memcpy(&bits, &v[i], sizeof(bits));
            mix(bits);
        }
        uint32_t last;
        std::memcpy(&last, &v.back(), sizeof(last));
        mix(last);
    };
    sample(a);
    sample(b);
    return h;
}

} // namespace

OpGpuServices::BinState OpGpuServices::binState(uint32_t kind,
                                                uint64_t fp,
                                                uint32_t count) {
    for (auto& q : binReqs_) {
        if (q.kind != kind || q.fp != fp) continue;
        BinState st{};
        st.slot = q.slot;
        if (!q.cached.empty()) {
            st.cached = &q.cached;
        } else if (mailboxReady(q.slot)) {
            auto bytes = mailboxTake(q.slot);
            q.cached.assign(bytes.size() / 4, 0u);
            std::memcpy(q.cached.data(), bytes.data(),
                        q.cached.size() * 4);
            if (q.cached.size() != count) q.cached.clear();
            if (!q.cached.empty()) st.cached = &q.cached;
        }
        return st;
    }
    BinReq req{kind, fp, allocMailbox(), count, {}};
    binReqs_.push_back(std::move(req));
    if (binReqs_.size() > 24) binReqs_.erase(binReqs_.begin());
    return BinState{nullptr, binReqs_.back().slot, true};
}

std::optional<std::vector<uint32_t>>
OpGpuServices::histBin(std::span<const float> data, uint32_t nBins,
                       float e0, float invW) {
    if (data.empty() || !nBins) return std::nullopt;
    const uint64_t fp = fpFloats(data);
    auto st = binState(0, fp, nBins);
    if (st.cached) return *st.cached;
    if (!st.isNew) return std::nullopt;   // readback still in flight
    uint32_t in = createBufferRaw(data.size_bytes() + 16, 1 | 2);
    writeBufferRaw(in, 0, data.data(), data.size_bytes());
    uint32_t bins = createBufferRaw(size_t(nBins) * 4 + 16, 2 | 16);
    stream_.emit(Op::HistBins,
                 PHistBins{in, uint32_t(data.size()), bins, e0, invW,
                           nBins, st.slot});
    return std::nullopt;
}

std::optional<std::vector<uint32_t>>
OpGpuServices::histBin2D(std::span<const float> x, std::span<const float> y,
                         uint32_t nBinsX, uint32_t nBinsY,
                         float x0, float invWX, float y0, float invWY) {
    const size_t n = std::min(x.size(), y.size());
    if (!n || !nBinsX || !nBinsY) return std::nullopt;
    const uint32_t count = nBinsX * nBinsY;
    const uint64_t fp = fpFloats(x.first(n), y.first(n));
    auto st = binState(1, fp, count);
    if (st.cached) return *st.cached;
    if (!st.isNew) return std::nullopt;
    // Interleave the samples: the shader reads vec2f pairs.
    std::vector<float> xy(n * 2);
    for (size_t i = 0; i < n; ++i) {
        xy[i * 2] = x[i];
        xy[i * 2 + 1] = y[i];
    }
    uint32_t in = createBufferRaw(xy.size() * 4 + 16, 1 | 2);
    writeBufferRaw(in, 0, xy.data(), xy.size() * 4);
    uint32_t bins = createBufferRaw(size_t(count) * 4 + 16, 2 | 16);
    stream_.emit(Op::HistBins2D,
                 PHistBins2D{in, uint32_t(n), bins, x0, invWX, y0, invWY,
                             nBinsX, nBinsY, st.slot});
    return std::nullopt;
}

std::optional<std::vector<float>>
OpGpuServices::fftSegments(std::span<const float> signal,
                           std::span<const float> win,
                           uint32_t n, uint32_t step, uint32_t numSegs) {
    // Workgroup storage caps the transform at 2048 complex points
    // (2 × n f32 = 16 KB, the spec-guaranteed workgroup limit).
    if (signal.empty() || win.size() < n || n < 2 || n > 2048 ||
        !numSegs) return std::nullopt;
    const size_t outCount = size_t(numSegs) * n * 2;
    const uint64_t fp = fpFloats(signal.first(std::min<size_t>(
        signal.size(), size_t(numSegs) * step + n)));
    auto st = binState(3, fp, uint32_t(outCount));
    if (st.cached) {
        std::vector<float> out(st.cached->size());
        std::memcpy(out.data(), st.cached->data(), out.size() * 4);
        return out;
    }
    if (!st.isNew) return std::nullopt;   // readback in flight

    uint32_t sig = createBufferRaw(signal.size_bytes() + 16, 1 | 2);
    writeBufferRaw(sig, 0, signal.data(), signal.size_bytes());
    uint32_t winBuf = createBufferRaw(win.size() * 4 + 16, 1 | 2);
    writeBufferRaw(winBuf, 0, win.data(), win.size() * 4);
    uint32_t out = createBufferRaw(outCount * 4 + 16, 2 | 16);
    stream_.emit(Op::FftSegments,
                 PFftSegments{sig, winBuf, out, n, step, numSegs,
                              uint32_t(signal.size()), st.slot});
    return std::nullopt;
}

bool OpGpuServices::contourTessellate(
    std::span<const float> grid, uint32_t w, uint32_t h,
    std::span<const float> levels, std::span<const plot::Color> colors,
    float bx, float ax, float by, float ay, float lineWidth,
    std::span<const float> dashes,
    render::GpuBuf& soupOut, render::GpuBuf& countOut) {
    if (w < 2 || h < 2 || levels.empty() ||
        levels.size() != colors.size()) return false;
    // Upper bound on emitted segments: every (cell, level) may cross.
    // 6 vertices per segment, 6 floats per vertex.
    const uint64_t maxSegs = uint64_t(w - 1) * (h - 1) * levels.size();

    // A dashed level re-strokes each segment as a run of dashes, so the
    // soup needs `dashMul` slots per segment. Segments never exceed the
    // cell diagonal, so the longest possible dash run is bounded by
    // that diagonal over the dash period.
    uint32_t dashMul = 1;
    for (size_t i = 0; i < levels.size(); ++i) {
        const float on = i * 2 + 1 < dashes.size() ? dashes[i * 2] : 0.0f;
        const float off = i * 2 + 1 < dashes.size() ? dashes[i * 2 + 1] : 0.0f;
        if (on <= 0.0f) continue;
        const float period = std::max(on + off, 0.5f);
        const float diag = std::sqrt(ax * ax + ay * ay);
        dashMul = std::max(dashMul,
                           uint32_t(std::ceil(diag / period)) + 1u);
    }
    dashMul = std::min(dashMul, 64u);

    const uint64_t maxVerts = maxSegs * 6 * dashMul;
    // Budget guard: a soup beyond this is not worth the memory; the
    // caller falls back to the CPU stroker.
    constexpr uint64_t kSoupBudget = 256ull << 20;
    if (maxVerts * 24 > kSoupBudget) return false;

    auto stage = [&](const void* d, size_t bytes) {
        uint32_t handle = createBufferRaw(bytes + 16, 1 | 2);
        writeBufferRaw(handle, 0, d, bytes);
        return handle;
    };
    const uint32_t gridBuf = stage(grid.data(), grid.size_bytes());
    const uint32_t levelsBuf = stage(levels.data(), levels.size_bytes());
    // Colours go across as vec4f (the shader reads array<vec4f>).
    std::vector<float> rgba(colors.size() * 4);
    for (size_t i = 0; i < colors.size(); ++i) {
        rgba[i * 4 + 0] = colors[i].r;
        rgba[i * 4 + 1] = colors[i].g;
        rgba[i * 4 + 2] = colors[i].b;
        rgba[i * 4 + 3] = colors[i].a;
    }
    const uint32_t colBuf = stage(rgba.data(), rgba.size() * 4);

    // Per-level dash pattern: (on, off) pairs, on == 0 → solid.
    std::vector<float> dashPairs(levels.size() * 2, 0.0f);
    for (size_t i = 0; i < levels.size() && i * 2 + 1 < dashes.size(); ++i) {
        dashPairs[i * 2] = dashes[i * 2];
        dashPairs[i * 2 + 1] = dashes[i * 2 + 1];
    }
    const uint32_t dashBuf = stage(dashPairs.data(),
                                   dashPairs.size() * 4);

    const uint32_t soup = createBufferRaw(maxVerts * 24 + 16, 2 | 16);
    // Indirect draw arguments: {vertexCount, instanceCount, 0, 0}. The
    // compute bumps vertexCount; instanceCount must already be 1.
    const uint32_t counter = createBufferRaw(16, 2 | 16 | 32);
    const uint32_t seed[4] = {0u, 1u, 0u, 0u};
    writeBufferRaw(counter, 0, seed, sizeof(seed));

    PContourTess payload{};
    payload.gridBuf = gridBuf;
    payload.levelsBuf = levelsBuf;
    payload.colBuf = colBuf;
    payload.outBuf = soup;
    payload.counterBuf = counter;
    payload.gridW = w;
    payload.gridH = h;
    payload.nLevels = uint32_t(levels.size());
    payload.bx = bx; payload.ax = ax;
    payload.by = by; payload.ay = ay;
    payload.hwidth = lineWidth * 0.5f;
    payload.maxVerts = uint32_t(maxVerts);
    payload.dashBuf = dashBuf;
    payload.dashMul = dashMul;
    stream_.emit(Op::ContourTess, payload);
    soupOut = render::GpuBuf(soup);
    countOut = render::GpuBuf(counter);
    return true;
}

bool OpGpuServices::triContourTessellate(
    std::span<const float> xyz,
    std::span<const uint32_t> tris,
    std::span<const float> levels,
    std::span<const plot::Color> colors,
    float bx, float ax, float by, float ay, float lineWidth,
    std::span<const float> dashes, uint32_t mode,
    render::GpuBuf& soupOut, render::GpuBuf& countOut) {
    const uint32_t nTris = uint32_t(tris.size() / 3);
    const uint32_t nLv = mode == 1 && levels.size() > 0
        ? uint32_t(levels.size() - 1) : uint32_t(levels.size());
    if (!nTris || !nLv || xyz.size() < 9) return false;

    // Upper bound: mode 0 → one stroked segment per (tri, level) ×6
    // verts ×dashMul; mode 1 → S–H clip caps a band polygon at 5 verts
    // → 3 triangles = 9 verts per (tri, band).
    uint32_t dashMul = 1;
    if (mode == 0) {
        for (size_t i = 0; i < nLv; ++i) {
            const float on = i * 2 + 1 < dashes.size() ? dashes[i * 2] : 0.f;
            const float off = i * 2 + 1 < dashes.size() ? dashes[i * 2 + 1]
                                                        : 0.f;
            if (on <= 0.0f) continue;
            const float period = std::max(on + off, 0.5f);
            const float diag = std::sqrt(ax * ax + ay * ay) * 2.0f;
            dashMul = std::max(dashMul,
                               uint32_t(std::ceil(diag / period)) + 1u);
        }
        dashMul = std::min(dashMul, 64u);
    }
    const uint64_t per = mode == 1 ? 9 : 6 * dashMul;
    const uint64_t maxVerts = uint64_t(nTris) * nLv * per;
    constexpr uint64_t kSoupBudget = 256ull << 20;
    if (!maxVerts || maxVerts * 24 > kSoupBudget) return false;

    auto stage = [&](const void* d, size_t bytes) {
        uint32_t handle = createBufferRaw(bytes + 16, 1 | 2 | 16);
        writeBufferRaw(handle, 0, d, bytes);
        return handle;
    };
    const uint32_t xyzBuf = stage(xyz.data(), xyz.size_bytes());
    const uint32_t trisBuf = stage(tris.data(), tris.size_bytes());
    const uint32_t levelsBuf = stage(levels.data(), levels.size_bytes());
    std::vector<float> rgba(colors.size() * 4);
    for (size_t i = 0; i < colors.size(); ++i) {
        rgba[i * 4 + 0] = colors[i].r;
        rgba[i * 4 + 1] = colors[i].g;
        rgba[i * 4 + 2] = colors[i].b;
        rgba[i * 4 + 3] = colors[i].a;
    }
    const uint32_t colBuf = stage(rgba.data(), rgba.size() * 4);
    std::vector<float> dashPairs(nLv * 2, 0.0f);
    for (size_t i = 0; i < nLv && i * 2 + 1 < dashes.size(); ++i) {
        dashPairs[i * 2] = dashes[i * 2];
        dashPairs[i * 2 + 1] = dashes[i * 2 + 1];
    }
    const uint32_t dashBuf = stage(dashPairs.data(), dashPairs.size() * 4);
    const uint32_t soup = createBufferRaw(maxVerts * 24 + 16, 2 | 16);
    const uint32_t counter = createBufferRaw(16, 2 | 16 | 32);
    const uint32_t seed[4] = {0u, 1u, 0u, 0u};
    writeBufferRaw(counter, 0, seed, sizeof(seed));

    PTriContourTess p{};
    p.xyzBuf = xyzBuf; p.trisBuf = trisBuf;
    p.levelsBuf = levelsBuf; p.colBuf = colBuf;
    p.outBuf = soup; p.counterBuf = counter; p.dashBuf = dashBuf;
    p.nTris = nTris; p.nLevels = nLv;
    p.bx = bx; p.ax = ax; p.by = by; p.ay = ay;
    p.hwidth = lineWidth * 0.5f;
    p.mode = mode; p.maxVerts = uint32_t(maxVerts); p.dashMul = dashMul;
    stream_.emit(Op::TriContourTess, p);
    soupOut = render::GpuBuf(soup);
    countOut = render::GpuBuf(counter);
    return true;
}

bool OpGpuServices::fillBetweenTess(
    std::span<const float> x, std::span<const float> y1,
    std::span<const float> y2, std::span<const uint32_t> mask,
    bool interpolate, float bx, float ax, float by, float ay,
    plot::Color color, render::GpuBuf& soupOut, render::GpuBuf& countOut) {
    const size_t n = std::min({x.size(), y1.size(), y2.size()});
    if (n < 2 || mask.size() < n) return false;
    // One quad per segment, plus one boundary triangle per masked run
    // edge — 9 verts per segment is a safe upper bound.
    const uint64_t maxVerts = uint64_t(n - 1) * 9;
    if (maxVerts * 24 > (256ull << 20)) return false;

    auto stage = [&](const void* d, size_t bytes) {
        const uint32_t handle = createBufferRaw(bytes + 16, 1 | 2 | 16);
        writeBufferRaw(handle, 0, d, bytes);
        return handle;
    };
    const uint32_t xBuf = stage(x.data(), n * sizeof(float));
    const uint32_t y1Buf = stage(y1.data(), n * sizeof(float));
    const uint32_t y2Buf = stage(y2.data(), n * sizeof(float));
    const uint32_t maskBuf = stage(mask.data(), n * sizeof(uint32_t));
    const uint32_t soup = createBufferRaw(maxVerts * 24 + 16, 2 | 16);
    const uint32_t counter = createBufferRaw(16, 2 | 16 | 32);
    const uint32_t seed[4] = {0u, 1u, 0u, 0u};
    writeBufferRaw(counter, 0, seed, sizeof(seed));

    PFillBetweenTess p{};
    p.xBuf = xBuf; p.y1Buf = y1Buf; p.y2Buf = y2Buf; p.maskBuf = maskBuf;
    p.outBuf = soup; p.counterBuf = counter;
    p.n = uint32_t(n);
    p.flags = interpolate ? 1u : 0u;
    p.maxVerts = uint32_t(maxVerts);
    p.bx = bx; p.ax = ax; p.by = by; p.ay = ay;
    p.r = color.r; p.g = color.g; p.b = color.b; p.a = color.a;
    stream_.emit(Op::FillBetweenTess, p);
    soupOut = render::GpuBuf(soup);
    countOut = render::GpuBuf(counter);
    return true;
}

bool OpGpuServices::quiverHeads(
    std::span<const float> segsPx,
    uint32_t mode, float hw2, float hl, float hal, plot::Color color,
    render::GpuBuf& soupOut, render::GpuBuf& countOut) {
    const uint32_t n = uint32_t(segsPx.size() / 4);
    if (!n) return false;
    const uint32_t perArrow = mode == 1 ? 6u : 3u;
    const uint64_t maxVerts = uint64_t(n) * perArrow;
    if (maxVerts * 24 > (256ull << 20)) return false;

    const uint32_t segBuf = createBufferRaw(segsPx.size_bytes() + 16,
                                            1 | 2 | 16);
    writeBufferRaw(segBuf, 0, segsPx.data(), segsPx.size_bytes());
    const uint32_t soup = createBufferRaw(maxVerts * 24 + 16, 2 | 16);
    const uint32_t counter = createBufferRaw(16, 2 | 16 | 32);
    const uint32_t seed[4] = {0u, 1u, 0u, 0u};
    writeBufferRaw(counter, 0, seed, sizeof(seed));

    PQuiverTess p{};
    p.segBuf = segBuf; p.outBuf = soup; p.counterBuf = counter;
    p.n = n; p.mode = mode; p.maxVerts = uint32_t(maxVerts);
    p.hw2 = hw2; p.hl = hl; p.hal = hal;
    p.r = color.r; p.g = color.g; p.b = color.b; p.a = color.a;
    stream_.emit(Op::QuiverTess, p);
    soupOut = render::GpuBuf(soup);
    countOut = render::GpuBuf(counter);
    return true;
}

std::optional<std::vector<uint32_t>>
OpGpuServices::hexBins(std::span<const float> x, std::span<const float> y,
                       uint32_t nx, uint32_t ny, float xMin, float yMin,
                       float sx, float sy) {
    const size_t n = std::min(x.size(), y.size());
    if (!n || !nx || !ny || sx == 0.0f || sy == 0.0f) return std::nullopt;
    const uint32_t count = (nx + 1) * (ny + 1) + nx * ny;
    const uint64_t fp = fpFloats(x.first(n), y.first(n));
    auto st = binState(2, fp, count);
    if (st.cached) return *st.cached;
    if (!st.isNew) return std::nullopt;
    std::vector<float> xy(n * 2);
    for (size_t i = 0; i < n; ++i) {
        xy[i * 2] = x[i];
        xy[i * 2 + 1] = y[i];
    }
    uint32_t in = createBufferRaw(xy.size() * 4 + 16, 1 | 2);
    writeBufferRaw(in, 0, xy.data(), xy.size() * 4);
    uint32_t out = createBufferRaw(size_t(count) * 4 + 16, 2 | 16);
    stream_.emit(Op::HexBins,
                 PHexBins{in, uint32_t(n), out, xMin, yMin, sx, sy,
                          nx, ny, st.slot});
    return std::nullopt;
}

std::optional<std::vector<float>>
OpGpuServices::kde1d(std::span<const float> data, float lo, float step,
                     float bw, uint32_t ne) {
    if (data.empty() || !ne) return std::nullopt;
    const float fp = data.front() + data[data.size() / 2] + data.back();
    for (auto& q : kdeReqs_) {
        if (q.n != data.size() || q.lo != lo || q.step != step ||
            q.bw != bw || q.ne != ne || q.fp != fp) continue;
        if (!q.cached.empty()) return q.cached;
        if (mailboxReady(q.slot)) {
            auto bytes = mailboxTake(q.slot);
            q.cached.resize(bytes.size() / 4);
            std::memcpy(q.cached.data(), bytes.data(),
                        q.cached.size() * 4);
            return q.cached;
        }
        return std::nullopt;  // readback in flight
    }
    Kde1dReq req{data.size(), lo, step, bw, ne, fp, allocMailbox(), {}};
    uint32_t in = createBufferRaw(data.size_bytes() + 16, 1 | 2);
    writeBufferRaw(in, 0, data.data(), data.size_bytes());
    uint32_t out = createBufferRaw(size_t(ne) * 4 + 16, 2 | 16);
    PViolinKde p{in, uint32_t(data.size()), out, ne, lo, step, bw,
                 req.slot};
    stream_.emit(Op::ViolinKde, p);
    kdeReqs_.push_back(std::move(req));
    if (kdeReqs_.size() > 16) kdeReqs_.erase(kdeReqs_.begin());
    return std::nullopt;
}

bool OpGpuServices::pcmTessellate(std::span<const float> x,
                                  std::span<const float> y,
                                  std::span<const float> t,
                                  std::span<const plot::Color> lut,
                                  uint32_t nCols, uint32_t nRows,
                                  bool gouraud, uint32_t flags,
                                  render::GpuBuf& posOut,
                                  render::GpuBuf& colOut) {
    const uint32_t cells = gouraud ? (nCols - 1) * (nRows - 1)
                                   : nCols * nRows;
    const uint32_t vertsPerCell = gouraud ? 12 : 6;
    const uint64_t nVerts = uint64_t(cells) * vertsPerCell;
    if (cells == 0 || nVerts > (1ull << 31)) return false;
    auto stage = [&](const void* d, size_t bytes) {
        uint32_t h = createBufferRaw(bytes + 16, 1 | 2);
        writeBufferRaw(h, 0, d, bytes);
        return h;
    };
    PPcmTess p{
        .xBuf = stage(x.data(), x.size_bytes()),
        .yBuf = stage(y.data(), y.size_bytes()),
        .tBuf = stage(t.data(), t.size_bytes()),
        .lutBuf = stage(lut.data(), lut.size_bytes()),
        .posBuf = createBufferRaw(nVerts * 8 + 16, 1 | 2),
        .colBuf = createBufferRaw(nVerts * 16 + 16, 1 | 2),
        .nCols = nCols, .nRows = nRows,
        .gouraud = uint32_t(gouraud), .flags = flags,
    };
    stream_.emit(Op::PcmTess, p);
    posOut = render::GpuBuf(p.posBuf);
    colOut = render::GpuBuf(p.colBuf);
    return true;
}

void OpGpuServices::destroyBuffer(render::GpuBuf buf) {
    releaseBuffer(uint32_t(buf));
}

void OpGpuServices::beginFrameScratch() {
    if (spine_) spine_->resetScratch();
    if (points_) points_->resetScratch();
    if (instPath_) instPath_->resetScratch();
    if (gpuLine_) gpuLine_->resetScratch();
    if (text_) text_->resetScratch();
}
std::unique_ptr<render::Cmd> OpGpuServices::beginPrePass() {
    if (gpuLine_) gpuLine_->resetScratch();
    return std::make_unique<OpCmd>(stream_);
}
void OpGpuServices::submitPrePass(std::unique_ptr<render::Cmd> /*cmd*/) {
    // Op stream: compute ops already recorded in order — nothing to do.
}

void OpGpuServices::ensureText() {
    if (!text_) {
        text_ = op::makeText(*this);
        text_->prepareAtlasGpu();
    }
    textReady_ = true;
}
void OpGpuServices::syncTextAtlas() {
    if (text_) text_->syncAtlas();
}


// ── mailbox plumbing ─────────────────────────────────────────────────
void OpGpuServices::deliverMailbox(uint32_t slot,
                                   float v0, float v1,
                                   float v2, float v3) {
    auto it = slotBuf_.find(slot);
    if (it == slotBuf_.end()) return;
    reduceResults_[it->second] =
        render::primitives::MinMax2D{v0, v1, v2, v3};
    slotBuf_.erase(it);
}
const render::primitives::MinMax2D*
OpGpuServices::reduceResult(uint32_t buf) const {
    auto it = reduceResults_.find(buf);
    return it == reduceResults_.end() ? nullptr : &it->second;
}
void OpGpuServices::trackReduceSlot(uint32_t slot, uint32_t buf) {
    slotBuf_[slot] = buf;
}

uintptr_t OpGpuServices::mailboxDest(uint32_t slot, uint32_t bytes) {
    auto& e = bulkMail_[slot];
    e.bytes.resize(bytes);
    e.ready = false;
    return reinterpret_cast<uintptr_t>(e.bytes.data());
}
void OpGpuServices::mailboxDone(uint32_t slot) {
    if (auto it = bulkMail_.find(slot); it != bulkMail_.end())
        it->second.ready = true;
}
bool OpGpuServices::mailboxReady(uint32_t slot) const {
    auto it = bulkMail_.find(slot);
    return it != bulkMail_.end() && it->second.ready;
}
std::vector<uint8_t> OpGpuServices::mailboxTake(uint32_t slot) {
    auto it = bulkMail_.find(slot);
    if (it == bulkMail_.end() || !it->second.ready) return {};
    auto v = std::move(it->second.bytes);
    bulkMail_.erase(it);
    return v;
}

} // namespace volcano::web
