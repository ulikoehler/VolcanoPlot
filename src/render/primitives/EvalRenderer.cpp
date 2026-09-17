// volcano/render/primitives/EvalRenderer.cpp — GPU function evaluation
#include "volcano/render/primitives/EvalRenderer.hpp"
#include "volcano/core/CommandBuffer.hpp"

#include <cmath>
#include <format>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {
constexpr uint32_t kWorkgroup = 256;

/// GLSL wrapper around the user body: `y = <expr>` or statements
/// assigning `y`. `x` is xMin + i*step (f64 range → f32 base+step).
std::string wrapBody(const std::string& body) {
    std::string assignment = body;
    // Bare expression (no '=' or ';' or braces) → wrap as `y = (expr);`.
    if (body.find('=') == std::string::npos &&
        body.find(';') == std::string::npos &&
        body.find('{') == std::string::npos)
        assignment = "y = (" + body + ");";
    return std::format(R"(
#version 460
layout(local_size_x = {}) in;

layout(set = 0, binding = 0) writeonly buffer OutBuf {{ vec2 data[]; }} outBuf;

layout(push_constant) uniform PC {{
    float xBase;
    float xStep;
    uint  count;
    uint  _pad;
}} pc;

void main() {{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    float x = pc.xBase + float(i) * pc.xStep;
    float y = 0.0;
    {}
    outBuf.data[i] = vec2(x, y);
}}
)", kWorkgroup, assignment);
}

uint32_t divRoundUp(uint32_t n, uint32_t d) { return (n + d - 1) / d; }

} // namespace

void EvalRenderer::init(vk::Device device, VmaAllocator allocator,
                        vk::Queue computeQueue, vk::CommandPool computePool) {
    device_ = device;
    allocator_ = allocator;
    computeQueue_ = computeQueue;
    computePool_ = computePool;

    vk::DescriptorSetLayoutBinding binding{};
    binding.setBinding(0)
           .setDescriptorType(vk::DescriptorType::eStorageBuffer)
           .setDescriptorCount(1)
           .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(binding);
    descLayout_ = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eCompute)
       .setOffset(0)
       .setSize(sizeof(float) * 4);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout_.get())
        .setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(4);
    vk::DescriptorPoolCreateInfo dpci{};
    dpci.setMaxSets(4).setPoolSizes(ps);
    descPool_ = device.createDescriptorPoolUnique(dpci);

    vk::DescriptorSetAllocateInfo dsai{};
    dsai.setDescriptorPool(descPool_.get())
        .setSetLayouts(descLayout_.get());
    descSet_ = device.allocateDescriptorSets(dsai).front();

    inited_ = true;
}

bool EvalRenderer::compile(const std::string& body) {
    if (!inited_) return false;
    std::vector<uint32_t> spv;
    try {
        spv = core::ShaderModule::compileGlsl(wrapBody(body), "comp");
    } catch (...) {
        spv.clear();
    }
    if (spv.empty()) { pipeline_.reset(); return false; }
    core::ShaderModule mod(device_, spv);
    if (!mod.handle()) { pipeline_.reset(); return false; }

    vk::ComputePipelineCreateInfo ci{};
    ci.setStage(vk::PipelineShaderStageCreateInfo{}
                    .setStage(vk::ShaderStageFlagBits::eCompute)
                    .setModule(mod.handle())
                    .setPName("main"))
      .setLayout(pipelineLayout_.get());
    auto res = device_.createComputePipelineUnique(nullptr, ci);
    if (res.result != vk::Result::eSuccess) {
        pipeline_.reset();
        return false;
    }
    pipeline_ = std::move(res.value);
    return true;
}

core::Buffer EvalRenderer::makeOutput(uint32_t count) const {
    core::BufferDesc desc{};
    desc.size = vk::DeviceSize(count) * sizeof(float) * 2;
    desc.usage = core::BufferUsage::VertexStorage;
    return core::Buffer(allocator_, desc);
}

void EvalRenderer::eval(vk::Buffer out, double xMin, double xMax,
                        uint32_t count) {
    if (!pipeline_ || !out || count == 0) return;

    // Bind this evaluation's output buffer.
    vk::DescriptorBufferInfo outInfo{};
    outInfo.setBuffer(out).setOffset(0).setRange(VK_WHOLE_SIZE);
    vk::WriteDescriptorSet w{};
    w.setDstSet(descSet_)
     .setDstBinding(0)
     .setDescriptorType(vk::DescriptorType::eStorageBuffer)
     .setBufferInfo(outInfo);
    device_.updateDescriptorSets(w, {});

    float xBase = float(xMin);
    float xStep = count > 1 ? float((xMax - xMin) / double(count - 1)) : 0.0f;

    struct PC {
        float xBase, xStep;
        uint32_t count, _pad;
    } pc{xBase, xStep, count, 0u};

    core::OneTimeCommands cmd(device_, computePool_, computeQueue_);
    auto cb = cmd.handle();
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_.get());
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          pipelineLayout_.get(), 0, descSet_, {});
    cb.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eCompute,
                     0, sizeof(pc), &pc);
    cb.dispatch(divRoundUp(count, kWorkgroup), 1, 1);
    // Barrier: compute writes → vertex attribute reads (and compute for
    // the next eval, since the descriptor set is re-pointed).
    vk::MemoryBarrier mb{};
    mb.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
      .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead |
                        vk::AccessFlagBits::eShaderWrite);
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                       vk::PipelineStageFlagBits::eVertexInput |
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, mb, {}, {});
}

} // namespace volcano::render::primitives
