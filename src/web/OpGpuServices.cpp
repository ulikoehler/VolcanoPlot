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

} // namespace volcano::web
