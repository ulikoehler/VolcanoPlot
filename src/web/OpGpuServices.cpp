// src/web/OpGpuServices.cpp — GpuServices over an OpStream
#include "OpGpuServices.hpp"
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
