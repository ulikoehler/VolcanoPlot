// volcano/encode/GpuPngEncoder.cpp
//
// GPU-side PNG scanline filtering: one workgroup per row evaluates all five
// PNG filters, picks the minimum-sum-of-absolute-differences filter (the
// libpng heuristic), and writes the filtered row. DEFLATE + container
// assembly stay on the CPU (zlib).
#include "volcano/encode/GpuPngEncoder.hpp"
#include "volcano/encode/PngEncoder.hpp"
#include "volcano/core/Buffer.hpp"
#include "volcano/core/CommandBuffer.hpp"
#include "volcano/core/ShaderModule.hpp"
#include "volcano/core/PipelineCache.hpp"
#include "volcano/core/DescriptorPool.hpp"

#ifdef VOLCANO_HAS_ZLIB
#include <zlib.h>
#endif

#include <cstring>
#include <stdexcept>

namespace volcano::encode {

namespace {

constexpr const char* kFilterGlsl = R"(
#version 460
layout(local_size_x = 256) in;

// Input RGBA8 pixels, one uint per pixel (little-endian R in low byte).
layout(set = 0, binding = 0) readonly buffer Src { uint px[]; } src;
// Output: row y occupies (w+1) uints; uint 0 low byte = filter type,
// uints 1..w = filtered pixels (byte order preserved).
layout(set = 0, binding = 1) writeonly buffer Dst { uint data[]; } dst;

layout(push_constant) uniform PC { uint w; uint h; } pc;

shared uint scores[5];
shared uint bestF;

uint absSigned(uint b) { return b < 128u ? b : 256u - b; }

uint paeth(uint a, uint b, uint c) {
    int p = int(a) + int(b) - int(c);
    int pa = abs(p - int(a)), pb = abs(p - int(b)), pq = abs(p - int(c));
    if (pa <= pb && pa <= pq) return a;
    return pb <= pq ? b : c;
}

// Filtered byte for component ci of pixel x in row y under filter f.
uint filtByte(uint f, uint x, uint y, uint ci) {
    uint rowBase = y * pc.w;
    uint sh = ci * 8u;
    uint cur = (src.px[rowBase + x] >> sh) & 0xffu;
    uint a = x > 0u        ? (src.px[rowBase + x - 1u] >> sh) & 0xffu : 0u;
    uint b = y > 0u        ? (src.px[rowBase - pc.w + x] >> sh) & 0xffu : 0u;
    uint c = (x > 0u && y > 0u) ? (src.px[rowBase - pc.w + x - 1u] >> sh) & 0xffu : 0u;
    if (f == 0u) return cur;
    if (f == 1u) return (cur - a) & 0xffu;
    if (f == 2u) return (cur - b) & 0xffu;
    if (f == 3u) return (cur - (a + b) / 2u) & 0xffu;
    return (cur - paeth(a, b, c)) & 0xffu;
}

void main() {
    uint y = gl_WorkGroupID.x;
    if (gl_LocalInvocationIndex < 5u) scores[gl_LocalInvocationIndex] = 0u;
    if (gl_LocalInvocationIndex == 0u) bestF = 0u;
    barrier();
    // Score every filter on this row (sum of |signed filtered byte|).
    for (uint f = 0u; f < 5u; ++f) {
        uint s = 0u;
        for (uint x = gl_LocalInvocationIndex; x < pc.w; x += 256u)
            for (uint ci = 0u; ci < 4u; ++ci)
                s += absSigned(filtByte(f, x, y, ci));
        atomicAdd(scores[f], s);
    }
    barrier();
    if (gl_LocalInvocationIndex == 0u) {
        uint bs = scores[0], bf = 0u;
        for (uint f = 1u; f < 5u; ++f)
            if (scores[f] < bs) { bs = scores[f]; bf = f; }
        bestF = bf;
        dst.data[y * (pc.w + 1u)] = bf;
    }
    barrier();
    uint f = bestF;
    for (uint x = gl_LocalInvocationIndex; x < pc.w; x += 256u) {
        uint v = 0u;
        for (uint ci = 0u; ci < 4u; ++ci)
            v |= filtByte(f, x, y, ci) << (ci * 8u);
        dst.data[y * (pc.w + 1u) + 1u + x] = v;
    }
}
)";

// --- minimal PNG container helpers (zlib crc32) ---
void putU32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}

#ifdef VOLCANO_HAS_ZLIB
void pngChunk(std::vector<uint8_t>& out, const char type[4],
              const std::vector<uint8_t>& data) {
    putU32be(out, uint32_t(data.size()));
    size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + at,
                      uInt(4 + data.size()));
    putU32be(out, uint32_t(crc));
}
#endif

} // namespace

struct GpuPngEncoder::Impl {
    vk::UniqueDescriptorSetLayout descLayout;
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;
    core::ShaderModule shader;
    bool ready = false;
};

GpuPngEncoder::GpuPngEncoder(vk::Device device, vk::Queue queue,
                             vk::CommandPool pool, VmaAllocator allocator)
    : device_(device), queue_(queue), pool_(pool), allocator_(allocator),
      impl_(std::make_unique<Impl>()) {
    auto spv = core::ShaderModule::compileGlsl(kFilterGlsl, "comp");
    impl_->shader = core::ShaderModule(device, spv);

    vk::DescriptorSetLayoutBinding bindings[2];
    bindings[0].setBinding(0).setDescriptorType(vk::DescriptorType::eStorageBuffer)
               .setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eCompute);
    bindings[1].setBinding(1).setDescriptorType(vk::DescriptorType::eStorageBuffer)
               .setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    impl_->descLayout = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eCompute).setOffset(0).setSize(8);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(impl_->descLayout.get()).setPushConstantRanges(pc);
    impl_->pipelineLayout = device.createPipelineLayoutUnique(plci);

    vk::ComputePipelineCreateInfo ci{};
    ci.stage.setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(impl_->shader.handle()).setPName("main");
    ci.setLayout(impl_->pipelineLayout.get());
    auto res = device.createComputePipelineUnique(nullptr, ci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("GpuPngEncoder pipeline failed");
    impl_->pipeline = std::move(res.value);

    impl_->ready = true;
}

GpuPngEncoder::~GpuPngEncoder() = default;

std::vector<uint8_t> GpuPngEncoder::filterScanlines(std::span<const uint8_t> rgba,
                                                    uint32_t w, uint32_t h) {
    if (!impl_ || !impl_->ready || w == 0 || h == 0) return {};
    if (rgba.size() < size_t(w) * h * 4) return {};

    const size_t inSize = size_t(w) * h * 4;
    const size_t outSize = size_t(h) * (w + 1) * 4; // per row: hdr uint + w px

    core::BufferDesc inDesc{};
    inDesc.size = inSize;
    inDesc.usage = core::BufferUsage::Storage;
    inDesc.hostVisible = true;
    core::Buffer inBuf(allocator_, inDesc);
    std::memcpy(inBuf.mappedData(), rgba.data(), inSize);

    core::BufferDesc outDesc{};
    outDesc.size = outSize;
    outDesc.usage = core::BufferUsage::Storage;
    outDesc.hostVisible = true;
    core::Buffer outBuf(allocator_, outDesc);

    // Fresh pool per call: cheap and keeps frames independent.
    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(2);
    core::DescriptorPool descPool(device_, {ps}, 1);
    vk::DescriptorSet set = descPool.allocate(impl_->descLayout.get());
    vk::DescriptorBufferInfo inInfo{}, outInfo{};
    inInfo.setBuffer(inBuf.handle()).setOffset(0).setRange(inSize);
    outInfo.setBuffer(outBuf.handle()).setOffset(0).setRange(outSize);
    vk::WriteDescriptorSet writes[2];
    writes[0].setDstSet(set).setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
             .setBufferInfo(inInfo);
    writes[1].setDstSet(set).setDstBinding(1)
             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
             .setBufferInfo(outInfo);
    device_.updateDescriptorSets(writes, {});

    {
        core::OneTimeCommands cmd(device_, pool_, queue_);
        cmd.handle().bindPipeline(vk::PipelineBindPoint::eCompute,
                                  impl_->pipeline.get());
        cmd.handle().bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                        impl_->pipelineLayout.get(), 0,
                                        set, {});
        uint32_t pcv[2] = {w, h};
        cmd.handle().pushConstants(impl_->pipelineLayout.get(),
                                   vk::ShaderStageFlagBits::eCompute, 0, 8, pcv);
        cmd.handle().dispatch(h, 1, 1);
    }

    outBuf.invalidate();
    const auto* out = static_cast<const uint32_t*>(outBuf.mappedData());
    // Repack [hdr uint | w px uints] rows -> PNG scanlines [1B filter | 4w B].
    std::vector<uint8_t> scan(size_t(h) * (1 + w * 4));
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t* row = out + size_t(y) * (w + 1);
        uint8_t* srow = scan.data() + size_t(y) * (1 + w * 4);
        srow[0] = uint8_t(row[0] & 0xff);
        std::memcpy(srow + 1, row + 1, size_t(w) * 4);
    }
    return scan;
}

EncodeResult GpuPngEncoder::encode(std::span<const uint8_t> rgba,
                                   uint32_t width, uint32_t height) {
#ifndef VOLCANO_HAS_ZLIB
    CpuPngEncoder cpu;
    return cpu.encode(rgba, width, height);
#else
    EncodeResult res;
    auto scan = filterScanlines(rgba, width, height);
    if (scan.empty()) { res.error = "GPU filter failed"; return res; }

    uLongf bound = compressBound(uLong(scan.size()));
    std::vector<uint8_t> z(bound);
    if (compress2(z.data(), &bound, scan.data(), uLong(scan.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        res.error = "deflate failed";
        return res;
    }
    z.resize(bound);

    res.bytes = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    putU32be(ihdr, width);
    putU32be(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    pngChunk(res.bytes, "IHDR", ihdr);
    pngChunk(res.bytes, "IDAT", z);
    pngChunk(res.bytes, "IEND", {});
    res.success = true;
    return res;
#endif
}

bool GpuPngEncoder::encodeToFile(std::span<const uint8_t> rgba, uint32_t w,
                                 uint32_t h, const std::filesystem::path& path) {
    auto res = encode(rgba, w, h);
    if (!res.success) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(res.bytes.data(), 1, res.bytes.size(), f);
    std::fclose(f);
    return true;
}

} // namespace volcano::encode
