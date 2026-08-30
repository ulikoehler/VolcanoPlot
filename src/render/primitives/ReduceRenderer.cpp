// volcano/render/primitives/ReduceRenderer.cpp — GPU parallel min/max reduce
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/core/CommandBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

constexpr uint32_t kWorkgroup = 256;

// Pass 1: input is raw vec2 points; output is vec4 partials (minX,maxX,minY,maxY).
constexpr const char* kReduceVec2Glsl = R"(
#version 460
layout(local_size_x = 256) in;

layout(set = 0, binding = 0) readonly buffer InBuf { vec2 data[]; } inBuf;
layout(set = 0, binding = 1) buffer OutBuf { vec4 data[]; } outBuf;

layout(push_constant) uniform PC {
    uint inputCount;
    uint isFinalPass;
} pc;

shared vec4 sh[256];

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint gid = gl_GlobalInvocationID.x;
    vec4 acc = vec4(1e38, -1e38, 1e38, -1e38); // minX, maxX, minY, maxY
    if (gid < pc.inputCount) {
        vec2 p = inBuf.data[gid];
        acc = vec4(p.x, p.x, p.y, p.y);
    }
    sh[tid] = acc;
    barrier();
    for (uint s = 128; s > 0; s >>= 1) {
        if (tid < s) {
            vec4 o = sh[tid + s];
            sh[tid].x = min(sh[tid].x, o.x);
            sh[tid].y = max(sh[tid].y, o.y);
            sh[tid].z = min(sh[tid].z, o.z);
            sh[tid].w = max(sh[tid].w, o.w);
        }
        barrier();
    }
    if (tid == 0) {
        if (pc.isFinalPass != 0u) outBuf.data[0] = sh[0];
        else outBuf.data[gl_WorkGroupID.x] = sh[0];
    }
}
)";

// Subsequent passes: input is vec4 partials; output is vec4 partials.
constexpr const char* kReduceVec4Glsl = R"(
#version 460
layout(local_size_x = 256) in;

layout(set = 0, binding = 0) readonly buffer InBuf { vec4 data[]; } inBuf;
layout(set = 0, binding = 1) buffer OutBuf { vec4 data[]; } outBuf;

layout(push_constant) uniform PC {
    uint inputCount;
    uint isFinalPass;
} pc;

shared vec4 sh[256];

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint gid = gl_GlobalInvocationID.x;
    vec4 acc = vec4(1e38, -1e38, 1e38, -1e38);
    if (gid < pc.inputCount) acc = inBuf.data[gid];
    sh[tid] = acc;
    barrier();
    for (uint s = 128; s > 0; s >>= 1) {
        if (tid < s) {
            vec4 o = sh[tid + s];
            sh[tid].x = min(sh[tid].x, o.x);
            sh[tid].y = max(sh[tid].y, o.y);
            sh[tid].z = min(sh[tid].z, o.z);
            sh[tid].w = max(sh[tid].w, o.w);
        }
        barrier();
    }
    if (tid == 0) {
        if (pc.isFinalPass != 0u) outBuf.data[0] = sh[0];
        else outBuf.data[gl_WorkGroupID.x] = sh[0];
    }
}
)";

uint32_t divRoundUp(uint32_t n, uint32_t d) {
    return (n + d - 1) / d;
}

} // namespace

void ReduceRenderer::init(vk::Device device, VmaAllocator allocator,
                          vk::Queue computeQueue, vk::CommandPool computePool) {
    device_ = device;
    allocator_ = allocator;
    computeQueue_ = computeQueue;
    computePool_ = computePool;

    auto v2Spv = core::ShaderModule::compileGlsl(kReduceVec2Glsl, "comp");
    auto v4Spv = core::ShaderModule::compileGlsl(kReduceVec4Glsl, "comp");
    reduceVec2_ = core::ShaderModule(device, v2Spv);
    reduceVec4_ = core::ShaderModule(device, v4Spv);

    // Descriptor set layout: binding 0 = input (storage), binding 1 = output (storage).
    vk::DescriptorSetLayoutBinding bindings[2];
    bindings[0].setBinding(0)
               .setDescriptorType(vk::DescriptorType::eStorageBuffer)
               .setDescriptorCount(1)
               .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    bindings[1].setBinding(1)
               .setDescriptorType(vk::DescriptorType::eStorageBuffer)
               .setDescriptorCount(1)
               .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    descLayout_ = device.createDescriptorSetLayoutUnique(dlci);

    // Pipeline layout with push constants.
    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eCompute)
       .setOffset(0)
       .setSize(sizeof(uint32_t) * 2);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout_.get())
        .setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    auto makePipe = [&](vk::ShaderModule module) {
        vk::ComputePipelineCreateInfo ci{};
        ci.setStage(vk::PipelineShaderStageCreateInfo{}
                        .setStage(vk::ShaderStageFlagBits::eCompute)
                        .setModule(module)
                        .setPName("main"))
           .setLayout(pipelineLayout_.get());
        auto res = device.createComputePipelineUnique(nullptr, ci);
        if (res.result != vk::Result::eSuccess)
            throw std::runtime_error("Failed to create reduce compute pipeline");
        return std::move(res.value);
    };
    pipeVec2_ = makePipe(reduceVec2_.handle());
    pipeVec4_ = makePipe(reduceVec4_.handle());

    // Dedicated descriptor pool with a small ring of pre-allocated sets.
    // Each reduce pass needs its own set (descriptor bindings cannot be
    // re-updated for a set already recorded into a command buffer). The
    // max number of passes for any realistic point count is tiny
    // (log_256(N) + 1 ≈ 4 for N = 10^9), so 8 is ample.
    descRingCap_ = 8;
    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(descRingCap_ * 2);
    vk::DescriptorPoolCreateInfo dpci{};
    dpci.setMaxSets(descRingCap_)
         .setPoolSizes(poolSize);
    descPool_ = device.createDescriptorPoolUnique(dpci);

    std::vector<vk::DescriptorSetLayout> layouts(descRingCap_, descLayout_.get());
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.setDescriptorPool(descPool_.get())
        .setSetLayouts(layouts);
    descSets_ = device.allocateDescriptorSets(dsai);

    // Host-visible output buffer for the final vec4 result.
    core::BufferDesc outDesc{};
    outDesc.size = sizeof(float) * 4;
    outDesc.usage = core::BufferUsage::Storage;
    outDesc.hostVisible = true;
    outDesc.hostCached = true;
    output_ = core::Buffer(allocator_, outDesc);

    inited_ = true;
}

void ReduceRenderer::ensureIntermediateCapacity(uint32_t slots) {
    if (slots <= interSlots_) return;
    vk::DeviceSize bytes = vk::DeviceSize(slots) * sizeof(float) * 4;
    core::BufferDesc desc{};
    desc.size = bytes;
    desc.usage = core::BufferUsage::Storage;
    // Device-local; written by compute, read by compute next pass.
    intermediateA_ = core::Buffer(allocator_, desc);
    intermediateB_ = core::Buffer(allocator_, desc);
    interSlots_ = slots;
}

void ReduceRenderer::recordPass(vk::CommandBuffer cmd, vk::DescriptorSet descSet,
                                vk::Buffer inBuf, uint32_t inCount,
                                vk::Buffer outBuf, bool isFinal, bool vec2Input) {
    // Update this pass's descriptor set bindings (host-side; takes effect
    // before the command buffer is submitted).
    vk::DescriptorBufferInfo inInfo{};
    inInfo.setBuffer(inBuf).setOffset(0).setRange(VK_WHOLE_SIZE);
    vk::DescriptorBufferInfo outInfo{};
    outInfo.setBuffer(outBuf).setOffset(0).setRange(VK_WHOLE_SIZE);
    vk::WriteDescriptorSet writes[2];
    writes[0].setDstSet(descSet)
             .setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
             .setBufferInfo(inInfo);
    writes[1].setDstSet(descSet)
             .setDstBinding(1)
             .setDescriptorType(vk::DescriptorType::eStorageBuffer)
             .setBufferInfo(outInfo);
    device_.updateDescriptorSets(writes, {});

    struct PC { uint32_t inputCount; uint32_t isFinalPass; } pc;
    pc.inputCount = inCount;
    pc.isFinalPass = isFinal ? 1u : 0u;

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute,
                     vec2Input ? pipeVec2_.get() : pipeVec4_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           pipelineLayout_.get(), 0, descSet, {});
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(PC), &pc);
    uint32_t groups = isFinal ? 1u : divRoundUp(inCount, kWorkgroup);
    cmd.dispatch(groups, 1, 1);

    if (isFinal) {
        // Ensure the host can see the device write to the host-visible output.
        vk::MemoryBarrier mb{};
        mb.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
           .setDstAccessMask(vk::AccessFlagBits::eHostRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eHost,
                            {}, mb, {}, {});
    } else {
        // Memory barrier between passes so the next read sees this pass's writes.
        vk::MemoryBarrier mb{};
        mb.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
           .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, mb, {}, {});
    }
}

std::optional<MinMax2D> ReduceRenderer::reduceMinMax2D(vk::Buffer pointBuffer,
                                                       uint32_t count) {
    if (!inited_ || count == 0 || !pointBuffer) return std::nullopt;

    uint32_t slots = divRoundUp(count, kWorkgroup);
    ensureIntermediateCapacity(std::max(slots, 1u));

    // Count passes to ensure we don't exceed the descriptor ring capacity.
    // Passes = number of times we halve-by-256 until <= 256, plus the final.
    uint32_t passes = 0;
    {
        uint32_t n = count;
        while (n > kWorkgroup) { n = divRoundUp(n, kWorkgroup); ++passes; }
        ++passes; // final
    }
    if (passes > descRingCap_) {
        // Pathological point count (> 256^8); fall back to CPU.
        return std::nullopt;
    }

    core::OneTimeCommands cmd(device_, computePool_, computeQueue_);
    auto cb = cmd.handle();

    vk::Buffer curIn = pointBuffer;
    uint32_t curCount = count;
    bool vec2Input = true;
    bool which = false; // false -> write A, true -> write B
    uint32_t setIdx = 0;

    while (curCount > kWorkgroup) {
        vk::Buffer outBuf = which ? intermediateB_.handle() : intermediateA_.handle();
        recordPass(cb, descSets_[setIdx++], curIn, curCount, outBuf,
                   /*isFinal=*/false, vec2Input);
        curIn = outBuf;
        curCount = divRoundUp(curCount, kWorkgroup);
        vec2Input = false;
        which = !which;
    }

    // Final pass: single workgroup -> host-visible output[0].
    recordPass(cb, descSets_[setIdx++], curIn, curCount, output_.handle(),
               /*isFinal=*/true, vec2Input);
    // OneTimeCommands destructor ends, submits, and waits on the compute queue.
    // The waitIdle provides host visibility for the device-written output.

    auto* out = static_cast<const float*>(output_.mappedData());
    if (!out) return std::nullopt;
    MinMax2D r;
    r.minX = out[0];
    r.maxX = out[1];
    r.minY = out[2];
    r.maxY = out[3];
    // Guard against an all-empty reduce (shouldn't happen since count > 0).
    if (!(r.minX <= r.maxX) || std::isinf(r.minX)) return std::nullopt;
    return r;
}

} // namespace volcano::render::primitives
