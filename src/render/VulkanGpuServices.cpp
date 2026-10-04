// src/render/VulkanGpuServices.cpp — Vulkan implementation of GpuServices
#include "VulkanGpuServices.hpp"
#include "VkFactory.hpp"
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

#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/text/TextRenderer.hpp>

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace volcano::render {

namespace {

/// vkPipelineCache blob path: $VOLCANO_CACHE_DIR, then
/// $XDG_CACHE_HOME/volcanoplot, then ~/.cache/volcanoplot. Empty = disabled.
/// Driver-specific blobs are validated by Vulkan on load, so stale or
/// foreign caches are safely ignored. Pipeline creation on lavapipe is
/// ~150 ms each — the cache pays for itself after the first run.
std::filesystem::path pipelineCacheFile() {
    std::filesystem::path dir;
    if (const char* d = std::getenv("VOLCANO_CACHE_DIR"); d && d[0]) {
        dir = d;
    } else if (const char* x = std::getenv("XDG_CACHE_HOME"); x && x[0]) {
        dir = std::filesystem::path(x) / "volcanoplot";
    } else if (const char* h = std::getenv("HOME"); h && h[0]) {
        dir = std::filesystem::path(h) / ".cache" / "volcanoplot";
    } else {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    return dir / "pipeline-cache.bin";
}




constexpr const char* kHistGlsl = R"GLSL(
#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer Src  { float v[]; } src;
layout(set = 0, binding = 1) buffer Bins { uint b[]; } dst;
layout(push_constant) uniform PC {
    uint n; uint nbins; float e0; float invW;
} pc;
shared uint sbins[1024];
void main() {
    uint lid = gl_LocalInvocationIndex;
    bool useShared = pc.nbins <= 1024u;
    if (useShared)
        for (uint i = lid; i < pc.nbins; i += 256u) sbins[i] = 0u;
    barrier();
    uint total = gl_NumWorkGroups.x * 256u;
    for (uint i = gl_GlobalInvocationID.x; i < pc.n; i += total) {
        float s = src.v[i];
        if (isnan(s) || isinf(s)) continue;
        if (s < pc.e0 || s > pc.e0 + float(pc.nbins) / pc.invW) continue;
        uint idx = uint((s - pc.e0) * pc.invW);
        if (idx >= pc.nbins) idx = pc.nbins - 1u;
        if (useShared) atomicAdd(sbins[idx], 1u);
        else           atomicAdd(dst.b[idx], 1u);
    }
    if (useShared) {
        barrier();
        for (uint i = lid; i < pc.nbins; i += 256u)
            if (sbins[i] > 0u) atomicAdd(dst.b[i], sbins[i]);
    }
}
)GLSL";

constexpr const char* kKde1dGlsl = R"GLSL(
#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer Src { float v[]; } src;
layout(set = 0, binding = 1) writeonly buffer Dst { float d[]; } dst;
layout(push_constant) uniform PC {
    uint ns;    // sample count
    uint ne;    // evaluation points
    float lo;   // first evaluation x
    float step; // evaluation spacing
    float bw;   // bandwidth
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.ne) return;
    float y = pc.lo + float(i) * pc.step;
    float sum = 0.0;
    for (uint s = 0u; s < pc.ns; ++s) {
        float t = (y - src.v[s]) / pc.bw;
        sum += exp(-0.5 * t * t) * 0.39894228;
    }
    dst.d[i] = sum / (float(pc.ns) * pc.bw);
}
)GLSL";

constexpr const char* kPcmTessGlsl = R"GLSL(
#version 460
layout(local_size_x = 256) in;

layout(set = 0, binding = 0) readonly buffer Xs { float v[]; } xs;
layout(set = 0, binding = 1) readonly buffer Ys { float v[]; } ys;
// Per-cell normalized t (flat shading) or per-corner t (gouraud).
layout(set = 0, binding = 2) readonly buffer Ts { float v[]; } ts;
// Colormap LUT: [0..255] = t∈[0,1], 256 = under, 257 = over, 258 = bad.
layout(set = 0, binding = 3) readonly buffer Lut { vec4 c[]; } lut;
layout(set = 0, binding = 4) writeonly buffer Pos { vec2 v[]; } pos;
layout(set = 0, binding = 5) writeonly buffer Col { vec4 v[]; } col;

layout(push_constant) uniform PC {
    uint nCols;   // cells per row (flat) or corner count (gouraud)
    uint nRows;
    uint gouraud;
    uint flags;   // bit0 = cmap.bad set, bit1 = skipNaN
} pc;

vec4 colorOf(float t) {
    if (isnan(t)) return lut.c[258];
    if (t < 0.0)  return lut.c[256];
    if (t > 1.0)  return lut.c[257];
    return lut.c[uint(t * 255.0 + 0.5)];
}

void main() {
    uint cell = gl_GlobalInvocationID.x;
    if (pc.gouraud == 0u) {
        if (cell >= pc.nCols * pc.nRows) return;
        uint i = cell % pc.nCols, j = cell / pc.nCols;
        vec4 c = colorOf(ts.v[cell]);
        vec2 bl = vec2(xs.v[i],   ys.v[j]);
        vec2 br = vec2(xs.v[i+1], ys.v[j]);
        vec2 ur = vec2(xs.v[i+1], ys.v[j+1]);
        vec2 ul = vec2(xs.v[i],   ys.v[j+1]);
        uint v = cell * 6u;
        pos.v[v]    = bl; pos.v[v+1u] = br; pos.v[v+2u] = ul;
        pos.v[v+3u] = br; pos.v[v+4u] = ur; pos.v[v+5u] = ul;
        for (uint k = 0u; k < 6u; ++k) col.v[v + k] = c;
    } else {
        // Gouraud: (nCols-1)*(nRows-1) quads, 4 tris meeting at the
        // averaged center vertex (mpl _convert_mesh_to_triangles).
        uint qw = pc.nCols - 1u;
        if (cell >= qw * (pc.nRows - 1u)) return;
        uint i = cell % qw, j = cell / qw;
        float ta = ts.v[j*pc.nCols + i],       tb = ts.v[j*pc.nCols + i+1];
        float tc = ts.v[(j+1u)*pc.nCols + i+1u], td = ts.v[(j+1u)*pc.nCols + i];
        vec4 ca = colorOf(ta), cb = colorOf(tb);
        vec4 cc = colorOf(tc), cd = colorOf(td);
        // mpl drops the quad if any corner is masked and no bad color.
        if ((pc.flags & 3u) == 2u && (isnan(ta) || isnan(tb) || isnan(tc) || isnan(td))) {
            ca = cb = cc = cd = vec4(0.0);
        }
        vec4 cCtr = (ca + cb + cc + cd) * 0.25;
        vec2 pa = vec2(xs.v[i],   ys.v[j]);
        vec2 pb = vec2(xs.v[i+1], ys.v[j]);
        vec2 pq = vec2(xs.v[i+1], ys.v[j+1]);
        vec2 pd = vec2(xs.v[i],   ys.v[j+1]);
        vec2 pCtr = (pa + pb + pq + pd) * 0.25;
        uint v = cell * 12u;
        vec2 pts[12] = vec2[12](pa, pb, pCtr, pb, pq, pCtr,
                                pq, pd, pCtr, pd, pa, pCtr);
        vec4 cls[12] = vec4[12](ca, cb, cCtr, cb, cc, cCtr,
                                cc, cd, cCtr, cd, ca, cCtr);
        for (uint k = 0u; k < 12u; ++k) {
            pos.v[v + k] = pts[k];
            col.v[v + k] = cls[k];
        }
    }
}
)GLSL";

/// Lazily-built compute pipeline (layout+pipe+module, one per kind).
struct ComputePipe {
    vk::UniqueDescriptorSetLayout descLayout;
    vk::UniquePipelineLayout pipeLayout;
    vk::UniquePipeline pipe;
    bool tried = false;

    bool build(vk::Device device, const char* glsl, uint32_t nBindings,
               uint32_t pcBytes) {
        if (tried) return static_cast<bool>(pipe);
        tried = true;
        auto spv = core::ShaderModule::compileGlsl(glsl, "comp");
        if (spv.empty()) return false;
        core::ShaderModule shader(device, spv);
        if (!shader.handle()) return false;
        std::vector<vk::DescriptorSetLayoutBinding> b(nBindings);
        for (uint32_t i = 0; i < nBindings; ++i)
            b[i].setBinding(i)
               .setDescriptorType(vk::DescriptorType::eStorageBuffer)
               .setDescriptorCount(1)
               .setStageFlags(vk::ShaderStageFlagBits::eCompute);
        vk::DescriptorSetLayoutCreateInfo dlci{};
        dlci.setBindings(b);
        descLayout = device.createDescriptorSetLayoutUnique(dlci);
        vk::PushConstantRange pcr{};
        pcr.setStageFlags(vk::ShaderStageFlagBits::eCompute)
           .setOffset(0).setSize(pcBytes);
        vk::PipelineLayoutCreateInfo plci{};
        plci.setSetLayouts(descLayout.get()).setPushConstantRanges(pcr);
        pipeLayout = device.createPipelineLayoutUnique(plci);
        vk::ComputePipelineCreateInfo ci{};
        ci.stage.setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(shader.handle()).setPName("main");
        ci.setLayout(pipeLayout.get());
        auto res = device.createComputePipelineUnique(nullptr, ci);
        if (res.result != vk::Result::eSuccess) return false;
        pipe = std::move(res.value);
        return true;
    }
};

// Process-lifetime: the Unique* members would call vkDestroy* during
// static teardown — after the device (and possibly the driver) is
// already gone. Leaking three small pipeline objects is the standard
// fix for GPU globals that outlive the context.
ComputePipe* histPipe_  = new ComputePipe();
ComputePipe* kde1dPipe_ = new ComputePipe();
ComputePipe* pcmPipe_   = new ComputePipe();

} // namespace

VulkanGpuServices::VulkanGpuServices(backend::GpuContext& ctx,
                                     vk::RenderPass renderPass,
                                     vk::Format colorFormat,
                                     vk::Format depthFormat,
                                     vk::SampleCountFlagBits samples)
    : ctx_(ctx), renderPass_(renderPass), colorFormat_(colorFormat),
      depthFormat_(depthFormat), samples_(samples) {}

VulkanGpuServices::~VulkanGpuServices() = default;

core::PipelineCache& VulkanGpuServices::pipelineCache() {
    if (!pipelineCache_)
        pipelineCache_ = std::make_unique<core::PipelineCache>(
            device(), pipelineCacheFile());
    return *pipelineCache_;
}

core::DescriptorPool& VulkanGpuServices::descPool() {
    if (!descPool_) {
        std::vector<vk::DescriptorPoolSize> sizes = {
            { vk::DescriptorType::eUniformBuffer, 256 },
            { vk::DescriptorType::eStorageBuffer, 256 },
            { vk::DescriptorType::eCombinedImageSampler, 64 },
        };
        descPool_ = std::make_unique<core::DescriptorPool>(
            device(), sizes, 512,
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    }
    return *descPool_;
}

// ── Buffer registry ─────────────────────────────────────────────────

vk::Buffer VulkanGpuServices::vkBufferOf(GpuBuf token) const {
    if (token == 0) return VK_NULL_HANDLE;
    if (token & (uint64_t(1) << 63)) {
        auto it = bufs_.find(token);
        return it == bufs_.end() ? VK_NULL_HANDLE
                                 : it->second.handle();
    }
    return vk::Buffer(VkBuffer(token));
}

core::Buffer* VulkanGpuServices::bufferOf(GpuBuf token) {
    auto it = bufs_.find(token);
    return it == bufs_.end() ? nullptr : &it->second;
}

GpuBuf VulkanGpuServices::adoptBuffer(core::Buffer&& buf) {
    const GpuBuf token = (nextBufToken_++) | (uint64_t(1) << 63);
    bufs_.emplace(token, std::move(buf));
    return token;
}

void VulkanGpuServices::oneTimeSubmit(std::function<void(vk::CommandBuffer)> fn) {
    core::OneTimeCommands c(device(), graphicsPool(), graphicsQueue());
    fn(c.handle());
}

GpuBuf VulkanGpuServices::createBuffer(const GpuBufferDesc& desc) {
    core::BufferDesc d{};
    d.size = desc.size;
    if (desc.vertex && desc.storage) d.usage = core::BufferUsage::VertexStorage;
    else if (desc.vertex)  d.usage = core::BufferUsage::Vertex;
    else if (desc.index)   d.usage = core::BufferUsage::Index;
    else if (desc.uniform) d.usage = core::BufferUsage::Uniform;
    else                   d.usage = core::BufferUsage::Storage;
    d.hostVisible = desc.hostVisible;
    return adoptBuffer(core::Buffer(allocator(), d));
}

void VulkanGpuServices::writeBuffer(GpuBuf buf, uint64_t offset,
                                    std::span<const std::byte> data) {
    if (auto* b = bufferOf(buf); b && b->mappedData()) {
        std::memcpy(static_cast<char*>(b->mappedData()) + offset,
                    data.data(), data.size());
        return;
    }
    // Device-local: stage + copy via one-time submit.
    core::BufferDesc sd{};
    sd.size = data.size();
    sd.usage = core::BufferUsage::Staging;
    sd.hostVisible = true;
    core::Buffer staging(allocator(), sd);
    std::memcpy(staging.mappedData(), data.data(), data.size());
    const vk::Buffer dst = vkBufferOf(buf);
    oneTimeSubmit([&](vk::CommandBuffer c) {
        vk::BufferCopy region{0, offset, data.size()};
        c.copyBuffer(staging.handle(), dst, region);
    });
}

void VulkanGpuServices::destroyBuffer(GpuBuf buf) {
    bufs_.erase(buf);
}

// ── Shared renderers ────────────────────────────────────────────────

void VulkanGpuServices::ensureGraphics() {
    if (graphicsReady_) return;
    spine_ = primitives::makeSpineVk(*this);
    instancedPath_ = primitives::makeInstancedPathVk(*this);
    gpuLine_ = primitives::makeGpuLineVk(*this);
    points_ = primitives::makePointVk(*this);
    reduce_ = primitives::makeReduceVk(*this);
    kdeEval_ = primitives::makeKdeEvalVk(*this);
    grid3D_ = makeGrid3DVk(*this);
    graphicsReady_ = true;
}

void VulkanGpuServices::ensureText() {
    if (!textInited_) {
        text_ = text::makeTextVk(*this);
        textInited_ = true;
        text_->prepareAtlasGpu();
        textReady_ = true;
    }
}

void VulkanGpuServices::syncTextAtlas() {
    if (text_) text_->syncAtlas();
}

primitives::SpineRenderer& VulkanGpuServices::spine() {
    ensureGraphics(); return *spine_;
}
primitives::PointRenderer& VulkanGpuServices::sharedPoints() {
    ensureGraphics(); return *points_;
}
primitives::InstancedPathRenderer& VulkanGpuServices::instancedPath() {
    ensureGraphics(); return *instancedPath_;
}
primitives::GpuLineRenderer& VulkanGpuServices::gpuLine() {
    ensureGraphics(); return *gpuLine_;
}
primitives::ReduceRenderer& VulkanGpuServices::reduce() {
    ensureGraphics(); return *reduce_;
}
primitives::KdeEvalRenderer& VulkanGpuServices::kdeEval() {
    ensureGraphics(); return *kdeEval_;
}
Grid3DRenderer& VulkanGpuServices::grid3D() {
    ensureGraphics(); return *grid3D_;
}
text::TextRenderer& VulkanGpuServices::text() {
    ensureText(); return *text_;
}

std::unique_ptr<primitives::PointRenderer>
VulkanGpuServices::createPointRenderer() {
    return primitives::makePointVk(*this);
}
std::unique_ptr<primitives::LineRenderer>
VulkanGpuServices::createLineRenderer() {
    return primitives::makeLineVk(*this);
}
std::unique_ptr<primitives::LineSegmentRenderer>
VulkanGpuServices::createLineSegmentRenderer() {
    return primitives::makeLineSegmentVk(*this);
}
std::unique_ptr<primitives::FillRenderer>
VulkanGpuServices::createFillRenderer() {
    return primitives::makeFillVk(*this);
}
std::unique_ptr<primitives::BarRenderer>
VulkanGpuServices::createBarRenderer() {
    return primitives::makeBarVk(*this);
}
std::unique_ptr<primitives::PieRenderer>
VulkanGpuServices::createPieRenderer() {
    return primitives::makePieVk(*this);
}
std::unique_ptr<primitives::HeatmapRenderer>
VulkanGpuServices::createHeatmapRenderer() {
    return primitives::makeHeatmapVk(*this);
}
std::unique_ptr<primitives::SurfaceRenderer>
VulkanGpuServices::createSurfaceRenderer() {
    return primitives::makeSurfaceVk(*this);
}
std::unique_ptr<primitives::InstancedPathRenderer>
VulkanGpuServices::createInstancedPathRenderer() {
    return primitives::makeInstancedPathVk(*this);
}
std::unique_ptr<primitives::EvalRenderer>
VulkanGpuServices::createEvalRenderer() {
    return primitives::makeEvalVk(*this);
}

// ── Frame lifecycle ─────────────────────────────────────────────────

void VulkanGpuServices::beginFrameScratch() {
    if (spine_) spine_->resetScratch();
    if (instancedPath_) instancedPath_->resetScratch();
    if (points_) points_->resetScratch();
    if (gpuLine_) gpuLine_->resetScratch();
    if (text_) text_->resetScratch();
}

std::unique_ptr<Cmd> VulkanGpuServices::beginPrePass() {
    preCmd_.emplace(device(), graphicsPool());
    preCmd_->begin();
    return std::make_unique<VkCmd>(preCmd_->handle());
}

void VulkanGpuServices::submitPrePass(std::unique_ptr<Cmd> /*cmd*/) {
    if (!preCmd_) return;
    preCmd_->end();
    vk::SubmitInfo submit{};
    vk::CommandBuffer pcb = preCmd_->handle();
    submit.setCommandBuffers(pcb);
    graphicsQueue().submit(submit);
    graphicsQueue().waitIdle();
    preCmd_.reset();
}

// ── Bespoke compute ─────────────────────────────────────────────────

std::optional<std::vector<uint32_t>>
VulkanGpuServices::histBin(std::span<const float> data, uint32_t nBins,
                           float e0, float invW) {
    const vk::Device dev = device();
    if (data.empty() || nBins == 0 || !histPipe_->build(dev, kHistGlsl, 2, 16))
        return std::nullopt;

    core::BufferDesc inDesc{};
    inDesc.size = data.size_bytes();
    inDesc.usage = core::BufferUsage::Storage;
    inDesc.hostVisible = true;
    core::Buffer inBuf(allocator(), inDesc);
    std::memcpy(inBuf.mappedData(), data.data(), data.size_bytes());

    const vk::DeviceSize binsBytes = vk::DeviceSize(nBins) * 4;
    core::BufferDesc binDesc{};
    binDesc.size = binsBytes;
    binDesc.usage = core::BufferUsage::Storage;
    binDesc.hostVisible = true;
    core::Buffer binBuf(allocator(), binDesc);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(2);
    core::DescriptorPool onePool(dev, {ps}, 1);
    vk::DescriptorSet set = onePool.allocate(histPipe_->descLayout.get());
    vk::DescriptorBufferInfo ii{}, oi{};
    ii.setBuffer(inBuf.handle()).setOffset(0).setRange(inDesc.size);
    oi.setBuffer(binBuf.handle()).setOffset(0).setRange(binsBytes);
    vk::WriteDescriptorSet w[2];
    w[0].setDstSet(set).setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(ii);
    w[1].setDstSet(set).setDstBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(oi);
    dev.updateDescriptorSets(w, {});

    {
        core::OneTimeCommands cmd(dev, graphicsPool(), graphicsQueue());
        cmd.handle().fillBuffer(binBuf.handle(), 0, binsBytes, 0);
        vk::MemoryBarrier2 bar{};
        bar.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
           .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
           .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
           .setDstAccessMask(vk::AccessFlagBits2::eShaderRead |
                             vk::AccessFlagBits2::eShaderWrite);
        vk::DependencyInfo dep{};
        dep.setMemoryBarriers(bar);
        cmd.handle().pipelineBarrier2(dep);
        cmd.handle().bindPipeline(vk::PipelineBindPoint::eCompute,
                                  histPipe_->pipe.get());
        cmd.handle().bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                        histPipe_->pipeLayout.get(), 0,
                                        set, {});
        struct { uint32_t n, nbins; float e0, invW; } pcv{
            uint32_t(data.size()), nBins, e0, invW};
        cmd.handle().pushConstants(histPipe_->pipeLayout.get(),
                                   vk::ShaderStageFlagBits::eCompute, 0,
                                   16, &pcv);
        uint32_t threads = uint32_t(std::min<size_t>(
            data.size(), size_t(64) * 1024));
        cmd.handle().dispatch((threads + 255) / 256, 1, 1);
    }
    binBuf.invalidate();
    std::vector<uint32_t> out(nBins);
    std::memcpy(out.data(), binBuf.mappedData(), binsBytes);
    return out;
}

std::optional<std::vector<float>>
VulkanGpuServices::kde1d(std::span<const float> data, float lo,
                        float step, float bw, uint32_t n) {
    const vk::Device dev = device();
    if (data.empty() || n == 0 || !kde1dPipe_->build(dev, kKde1dGlsl, 2, 20))
        return std::nullopt;

    core::BufferDesc sd{};
    sd.size = data.size_bytes();
    sd.usage = core::BufferUsage::Storage;
    sd.hostVisible = true;
    core::Buffer srcBuf(allocator(), sd);
    std::memcpy(srcBuf.mappedData(), data.data(), sd.size);

    core::BufferDesc dd{};
    dd.size = size_t(n) * 4;
    dd.usage = core::BufferUsage::Storage;
    dd.hostVisible = true;
    dd.hostCached = true;
    core::Buffer dstBuf(allocator(), dd);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(2);
    core::DescriptorPool onePool(dev, {ps}, 1);
    vk::DescriptorSet set = onePool.allocate(kde1dPipe_->descLayout.get());
    vk::DescriptorBufferInfo ii{}, oi{};
    ii.setBuffer(srcBuf.handle()).setOffset(0).setRange(sd.size);
    oi.setBuffer(dstBuf.handle()).setOffset(0).setRange(dd.size);
    vk::WriteDescriptorSet w[2];
    w[0].setDstSet(set).setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(ii);
    w[1].setDstSet(set).setDstBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(oi);
    dev.updateDescriptorSets(w, {});

    {
        core::OneTimeCommands cmd(dev, graphicsPool(), graphicsQueue());
        cmd.handle().bindPipeline(vk::PipelineBindPoint::eCompute,
                                  kde1dPipe_->pipe.get());
        cmd.handle().bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                        kde1dPipe_->pipeLayout.get(), 0,
                                        set, {});
        struct { uint32_t ns, ne; float lo, step, bw; } pcv{
            uint32_t(data.size()), n, lo, step, bw};
        cmd.handle().pushConstants(kde1dPipe_->pipeLayout.get(),
                                   vk::ShaderStageFlagBits::eCompute, 0,
                                   20, &pcv);
        cmd.handle().dispatch((n + 255) / 256, 1, 1);
    }
    dstBuf.invalidate();
    std::vector<float> out(n);
    std::memcpy(out.data(), dstBuf.mappedData(), dd.size);
    return out;
}

bool VulkanGpuServices::pcmTessellate(
        std::span<const float> x, std::span<const float> y,
        std::span<const float> t, std::span<const plot::Color> lut,
        uint32_t nCols, uint32_t nRows, bool gouraud,
        uint32_t flags, GpuBuf& posOut, GpuBuf& colOut) {
    const vk::Device dev = device();
    const uint32_t cells = gouraud ? (nCols - 1) * (nRows - 1)
                                   : nCols * nRows;
    const uint32_t vertsPerCell = gouraud ? 12 : 6;
    const uint64_t nVerts = uint64_t(cells) * vertsPerCell;
    if (cells == 0 || nVerts > (1ull << 31)) return false;
    if (!pcmPipe_->build(dev, kPcmTessGlsl, 6, 16)) return false;

    auto mkStorage = [&](const void* data, size_t bytes) {
        core::BufferDesc d{};
        d.size = bytes;
        d.usage = core::BufferUsage::Storage;
        core::Buffer buf(allocator(), d);
        buf.upload(dev, graphicsQueue(), graphicsPool(),
                   std::span{static_cast<const std::byte*>(data), bytes});
        return buf;
    };
    core::Buffer xBuf = mkStorage(x.data(), x.size_bytes());
    core::Buffer yBuf = mkStorage(y.data(), y.size_bytes());
    core::Buffer tBuf = mkStorage(t.data(), t.size_bytes());
    core::Buffer lutBuf = mkStorage(lut.data(),
                                    lut.size() * sizeof(plot::Color));

    core::BufferDesc pdesc{};
    pdesc.size = nVerts * sizeof(plot::Point2D);
    pdesc.usage = core::BufferUsage::VertexStorage;
    core::Buffer posBuf(allocator(), pdesc);
    core::BufferDesc cdesc{};
    cdesc.size = nVerts * sizeof(plot::Color);
    cdesc.usage = core::BufferUsage::VertexStorage;
    core::Buffer colBuf(allocator(), cdesc);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(6);
    core::DescriptorPool descPool(dev, {ps}, 1);
    vk::DescriptorSet dset = descPool.allocate(pcmPipe_->descLayout.get());

    vk::DescriptorBufferInfo infos[6];
    std::array<std::pair<vk::Buffer, vk::DeviceSize>, 6> bufs{{
        {xBuf.handle(), xBuf.size()}, {yBuf.handle(), yBuf.size()},
        {tBuf.handle(), tBuf.size()}, {lutBuf.handle(), lutBuf.size()},
        {posBuf.handle(), posBuf.size()}, {colBuf.handle(), colBuf.size()}}};
    vk::WriteDescriptorSet writes[6];
    for (uint32_t b = 0; b < 6; ++b) {
        infos[b].setBuffer(bufs[b].first).setOffset(0).setRange(bufs[b].second);
        writes[b].setDstSet(dset).setDstBinding(b)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setBufferInfo(infos[b]);
    }
    dev.updateDescriptorSets(writes, {});

    {
        core::OneTimeCommands cmd(dev, graphicsPool(), graphicsQueue());
        auto c = cmd.handle();
        c.bindPipeline(vk::PipelineBindPoint::eCompute,
                       pcmPipe_->pipe.get());
        c.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             pcmPipe_->pipeLayout.get(), 0, dset, {});
        uint32_t pcv[4] = {nCols, nRows, gouraud ? 1u : 0u, flags};
        c.pushConstants(pcmPipe_->pipeLayout.get(),
                        vk::ShaderStageFlagBits::eCompute, 0, 16, pcv);
        c.dispatch((cells + 255) / 256, 1, 1);
        vk::BufferMemoryBarrier barriers[2];
        for (uint32_t b = 0; b < 2; ++b) {
            barriers[b].setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead)
                .setBuffer(bufs[4 + b].first).setOffset(0)
                .setSize(bufs[4 + b].second);
        }
        c.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eVertexInput,
                          {}, {}, barriers, {});
    }

    posOut = adoptBuffer(std::move(posBuf));
    colOut = adoptBuffer(std::move(colBuf));
    return true;
}

std::unique_ptr<primitives::SpineRenderer>
VulkanGpuServices::createSpineRenderer() {
    return primitives::makeSpineVk(*this);
}
std::unique_ptr<primitives::GpuLineRenderer>
VulkanGpuServices::createGpuLineRenderer() {
    return primitives::makeGpuLineVk(*this);
}
std::unique_ptr<text::TextRenderer>
VulkanGpuServices::createTextRenderer() {
    return text::makeTextVk(*this);
}

} // namespace volcano::render
