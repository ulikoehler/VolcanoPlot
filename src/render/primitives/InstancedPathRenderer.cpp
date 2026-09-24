// volcano/render/primitives/InstancedPathRenderer.cpp
#include "volcano/render/primitives/InstancedPathRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <cstring>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
// Template-space vertex (binding 0, per-vertex).
layout(location = 0) in vec2 a_pos;
// Per-instance attributes (binding 1, per-instance).
layout(location = 1) in vec2 i_offset;
layout(location = 2) in vec2 i_size;
layout(location = 3) in vec4 i_color;
layout(push_constant) uniform PC {
    vec2 u_resolution;
} pc;
layout(location = 0) out vec4 v_color;
void main() {
    vec2 px = i_offset + a_pos * i_size;
    vec2 ndc = vec2(
        px.x / pc.u_resolution.x * 2.0 - 1.0,
        px.y / pc.u_resolution.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = i_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = v_color;
}
)";

} // namespace

void InstancedPathRenderer::init(vk::Device device, VmaAllocator allocator,
                                 vk::RenderPass renderPass,
                                 vk::SampleCountFlagBits samples,
                                 core::PipelineCache& /*cache*/,
                                 core::DescriptorPool& /*descPool*/) {
    device_ = device;
    allocator_ = allocator;

    auto vertSpv = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, vertSpv);
    frag_ = core::ShaderModule(device, fragSpv);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 2);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bindings[2];
    bindings[0].setBinding(0).setStride(sizeof(float) * 2)
               .setInputRate(vk::VertexInputRate::eVertex);
    bindings[1].setBinding(1).setStride(sizeof(PathInstance))
               .setInputRate(vk::VertexInputRate::eInstance);

    vk::VertexInputAttributeDescription attrs[4];
    attrs[0].setLocation(0).setBinding(0)
            .setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    attrs[1].setLocation(1).setBinding(1)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(offsetof(PathInstance, ox));
    attrs[2].setLocation(2).setBinding(1)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(offsetof(PathInstance, sx));
    attrs[3].setLocation(3).setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(PathInstance, r));

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(bindings)
         .setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att{};
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorBlendOp(vk::BlendOp::eAdd)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR
                        | vk::ColorComponentFlagBits::eG
                        | vk::ColorComponentFlagBits::eB
                        | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci{};
    cbsci.setAttachments(att);

    vk::DynamicState dynStates[] = { vk::DynamicState::eViewport,
                                     vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci{};
    dsci.setDynamicStates(dynStates);

    vk::GraphicsPipelineCreateInfo gpci{};
    gpci.setStages(stages)
        .setPVertexInputState(&visci)
        .setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci)
        .setPRasterizationState(&rsci)
        .setPMultisampleState(&msci)
        .setPDepthStencilState(&depthState)
        .setPColorBlendState(&cbsci)
        .setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get())
        .setRenderPass(renderPass);

    auto rv = device.createGraphicsPipelineUnique({}, gpci);
    pipeline_ = std::move(rv.value);

    inited_ = true;
}

void InstancedPathRenderer::setTemplate(vk::Device device, vk::Queue queue,
                                        vk::CommandPool pool,
                                        std::span<const plot::Point2D> triVerts) {
    templateVerts_ = uint32_t(triVerts.size());
    if (triVerts.empty()) { templateVB_ = {}; return; }
    core::BufferDesc bdesc{};
    bdesc.size = triVerts.size() * sizeof(plot::Point2D);
    bdesc.usage = core::BufferUsage::Vertex;
    templateVB_ = core::Buffer(allocator_, bdesc);
    templateVB_.upload(device, queue, pool,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(triVerts.data()),
            bdesc.size));
}

void InstancedPathRenderer::ensureScratch(size_t byteCount) {
    if (scratchOffset_ + byteCount <= scratchCapacity_) return;
    size_t needed = scratchOffset_ + byteCount;
    size_t newSize = std::max<size_t>(1u << 20, needed * 2);
    // The old buffer is still referenced by draw commands recorded this
    // frame — keep it alive until resetScratch() at the next frame.
    if (scratchVB_.handle() != VK_NULL_HANDLE)
        retiredScratch_.push_back(std::move(scratchVB_));
    core::BufferDesc bdesc{};
    bdesc.size = newSize;
    bdesc.usage = core::BufferUsage::Vertex;
    bdesc.hostVisible = true;
    scratchVB_ = core::Buffer(allocator_, bdesc);
    scratchCapacity_ = newSize;
    scratchOffset_ = 0;
}

void InstancedPathRenderer::drawInstanced(
        vk::CommandBuffer cmd, vk::Rect2D clip, vk::Extent2D resolution,
        std::span<const PathInstance> instances) {
    if (!inited_ || instances.empty() || templateVerts_ == 0) return;

    size_t byteSize = instances.size() * sizeof(PathInstance);
    ensureScratch(byteSize);
    std::memcpy(static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_,
                instances.data(), byteSize);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());

    struct PC { float w, h; } pc{
        float(resolution.width), float(resolution.height)};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize zero = 0;
    cmd.bindVertexBuffers(0, templateVB_.handle(), zero);
    vk::DeviceSize instOff = vk::DeviceSize(scratchOffset_);
    cmd.bindVertexBuffers(1, scratchVB_.handle(), instOff);

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, clip);

    cmd.draw(templateVerts_, uint32_t(instances.size()), 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

} // namespace volcano::render::primitives
