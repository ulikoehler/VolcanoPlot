// volcano/render/primitives/KdeEvalRenderer.cpp — GPU KDE evaluation
#include "volcano/render/primitives/KdeEvalRenderer.hpp"
#include "volcano/core/CommandBuffer.hpp"
#include "volcano/plot/Types.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

/// One workgroup thread per output cell; each gathers all samples.
constexpr const char* kKdeGlsl = R"(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) readonly buffer InBuf { vec2 s[]; } samples;
layout(set = 0, binding = 1) writeonly buffer OutBuf { float g[]; } grid;

layout(push_constant) uniform PC {
    uint  nSamples;
    uint  gridW;
    uint  gridH;
    uint  _pad;
    float xMin;  float xStep;
    float yMin;  float yStep;
    float inv2bwX2;  // 1/(2*bwX^2)
    float inv2bwY2;
    float norm;      // 1/(2*pi*bwX*bwY*N)
    float _pad2;
} pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint j = gl_GlobalInvocationID.y;
    if (i >= pc.gridW || j >= pc.gridH) return;
    float gx = pc.xMin + (float(i) + 0.5) * pc.xStep;
    float gy = pc.yMin + (float(j) + 0.5) * pc.yStep;
    float sum = 0.0;
    for (uint k = 0; k < pc.nSamples; ++k) {
        vec2 s = samples.s[k];
        float dx = gx - s.x;
        float dy = gy - s.y;
        sum += exp(-(dx * dx * pc.inv2bwX2 + dy * dy * pc.inv2bwY2));
    }
    grid.g[j * pc.gridW + i] = sum * pc.norm;
}
)";

} // namespace

void KdeEvalRenderer::init(vk::Device device, VmaAllocator allocator,
                           vk::Queue computeQueue,
                           vk::CommandPool computePool) {
    device_ = device;
    allocator_ = allocator;
    computeQueue_ = computeQueue;
    computePool_ = computePool;

    vk::DescriptorSetLayoutBinding bindings[2];
    for (int i = 0; i < 2; ++i)
        bindings[i].setBinding(i)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(bindings);
    descLayout_ = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eCompute)
       .setOffset(0).setSize(sizeof(float) * 8 + sizeof(uint32_t) * 4);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout_.get()).setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    auto spv = core::ShaderModule::compileGlsl(kKdeGlsl, "comp");
    core::ShaderModule mod(device, spv);
    vk::ComputePipelineCreateInfo ci{};
    ci.setStage(vk::PipelineShaderStageCreateInfo{}
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(mod.handle()).setPName("main"))
      .setLayout(pipelineLayout_.get());
    auto res = device.createComputePipelineUnique(nullptr, ci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("KDE compute pipeline creation failed");
    pipeline_ = std::move(res.value);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(4);
    vk::DescriptorPoolCreateInfo dpci{};
    dpci.setMaxSets(2).setPoolSizes(ps);
    descPool_ = device.createDescriptorPoolUnique(dpci);
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.setDescriptorPool(descPool_.get())
        .setSetLayouts(descLayout_.get());
    descSet_ = device.allocateDescriptorSets(dsai).front();

    inited_ = true;
}

std::vector<float> KdeEvalRenderer::eval(
        const std::vector<plot::Point2D>& samples,
        uint32_t gridW, uint32_t gridH,
        float xMin, float xMax, float yMin, float yMax,
        float bwX, float bwY) {
    if (!inited_ || samples.empty() || gridW == 0 || gridH == 0)
        return {};

    // (Re)allocate buffers at capacity.
    if (sampleCap_ < samples.size()) {
        core::BufferDesc sd{};
        sd.size = vk::DeviceSize(samples.size()) * sizeof(float) * 2;
        sd.usage = core::BufferUsage::Storage;
        sampleBuf_ = core::Buffer(allocator_, sd);
        sampleCap_ = uint32_t(samples.size());
    }
    uint32_t cells = gridW * gridH;
    if (gridCap_ < cells) {
        core::BufferDesc gd{};
        gd.size = vk::DeviceSize(cells) * sizeof(float);
        gd.usage = core::BufferUsage::Storage;
        gd.hostVisible = true;
        gd.hostCached = true;
        gridBuf_ = core::Buffer(allocator_, gd);
        gridCap_ = cells;
    }

    sampleBuf_.upload(device_, computeQueue_, computePool_,
        std::as_bytes(std::span{samples.data(), samples.size()}));

    vk::DescriptorBufferInfo inInfo{};
    inInfo.setBuffer(sampleBuf_.handle()).setOffset(0).setRange(VK_WHOLE_SIZE);
    vk::DescriptorBufferInfo outInfo{};
    outInfo.setBuffer(gridBuf_.handle()).setOffset(0).setRange(VK_WHOLE_SIZE);
    vk::WriteDescriptorSet writes[2];
    writes[0].setDstSet(descSet_).setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(inInfo);
    writes[1].setDstSet(descSet_).setDstBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(outInfo);
    device_.updateDescriptorSets(writes, {});

    struct PC {
        uint32_t nSamples, gridW, gridH, pad;
        float xMin, xStep, yMin, yStep, inv2bwX2, inv2bwY2, norm, pad2;
    } pc{};
    pc.nSamples = uint32_t(samples.size());
    pc.gridW = gridW; pc.gridH = gridH;
    pc.xMin = xMin; pc.xStep = (xMax - xMin) / float(gridW);
    pc.yMin = yMin; pc.yStep = (yMax - yMin) / float(gridH);
    pc.inv2bwX2 = 1.0f / (2.0f * bwX * bwX);
    pc.inv2bwY2 = 1.0f / (2.0f * bwY * bwY);
    constexpr float kTwoPi = 6.28318530718f;
    pc.norm = 1.0f / (kTwoPi * bwX * bwY * float(samples.size()));

    {
        core::OneTimeCommands cmd(device_, computePool_, computeQueue_);
        auto cb = cmd.handle();
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_.get());
        cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                              pipelineLayout_.get(), 0, descSet_, {});
        cb.pushConstants(pipelineLayout_.get(),
                         vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(pc), &pc);
        cb.dispatch((gridW + 7) / 8, (gridH + 7) / 8, 1);
        vk::MemoryBarrier mb{};
        mb.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
          .setDstAccessMask(vk::AccessFlagBits::eHostRead);
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eHost, {}, mb, {}, {});
    }

    std::vector<float> out(cells);
    std::memcpy(out.data(), gridBuf_.mappedData(),
                cells * sizeof(float));
    return out;
}

} // namespace volcano::render::primitives
