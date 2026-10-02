// volcano/render/primitives/InstancedPathRenderer.cpp
#include "volcano/render/primitives/InstancedPathRenderer.hpp"
#include "../VkFactory.hpp"
#include "../VulkanGpuServices.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <cstring>
#include <volcano/core/ShaderModule.hpp>

namespace volcano::render::primitives {

namespace {

[[nodiscard]] inline vk::Rect2D vkScissor(plot::Rect2D r) noexcept {
    return {vk::Offset2D{static_cast<int32_t>(r.x),
                        static_cast<int32_t>(r.y)},
            vk::Extent2D{r.width, r.height}};
}


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

class InstancedPathRendererVk final : public InstancedPathRenderer {
public:
    explicit InstancedPathRendererVk(VulkanGpuServices& svcs)
        : svcs_(&svcs) {}

    void init();

    void setTemplate(std::span<const plot::Point2D> triVerts) override;
    void drawInstanced(Cmd& cmd, plot::Rect2D clip,
                       plot::Extent2D resolution,
                       std::span<const PathInstance> instances) override;
    void resetScratch() override {
        scratchOffset_ = 0;
        retiredScratch_.clear();
        retiredTemplates_.clear();
    }

    [[nodiscard]] bool inited() const noexcept override { return inited_; }
    [[nodiscard]] uint32_t templateVertCount() const noexcept override {
        return templateVerts_;
    }

private:
    void ensureScratch(size_t byteCount);

    VulkanGpuServices* svcs_ = nullptr;
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    bool inited_ = false;

    core::Buffer templateVB_;
    uint32_t templateVerts_ = 0;
    std::vector<core::Buffer> retiredTemplates_;

    core::Buffer scratchVB_;
    std::vector<core::Buffer> retiredScratch_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;
};

} // namespace

void InstancedPathRendererVk::init() {
    const vk::Device device = svcs_->device();
    const auto renderPass = svcs_->renderPass();
    const auto samples = svcs_->samples();
    device_ = device;
    allocator_ = svcs_->allocator();

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

void InstancedPathRendererVk::setTemplate(
        std::span<const plot::Point2D> triVerts) {
    const vk::Device device = svcs_->device();
    const vk::Queue queue = svcs_->graphicsQueue();
    const vk::CommandPool pool = svcs_->graphicsPool();
    templateVerts_ = uint32_t(triVerts.size());
    if (triVerts.empty()) { templateVB_ = {}; return; }
    // A draw recorded earlier this frame may still reference the old
    // template buffer — retire it instead of freeing.
    if (templateVB_.handle() != VK_NULL_HANDLE)
        retiredTemplates_.push_back(std::move(templateVB_));
    core::BufferDesc bdesc{};
    bdesc.size = triVerts.size() * sizeof(plot::Point2D);
    bdesc.usage = core::BufferUsage::Vertex;
    templateVB_ = core::Buffer(allocator_, bdesc);
    templateVB_.upload(device, queue, pool,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(triVerts.data()),
            bdesc.size));
}

void InstancedPathRendererVk::ensureScratch(size_t byteCount) {
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

void InstancedPathRendererVk::drawInstanced(
        Cmd& cmdRef, plot::Rect2D clip, plot::Extent2D resolution,
        std::span<const PathInstance> instances) {
    const auto cmd = vkCmd(cmdRef);
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
    cmd.setScissor(0, vkScissor(clip));

    cmd.draw(templateVerts_, uint32_t(instances.size()), 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

std::unique_ptr<InstancedPathRenderer>
makeInstancedPathVk(VulkanGpuServices& svcs) {
    auto p = std::make_unique<InstancedPathRendererVk>(svcs);
    p->init();
    return p;
}

} // namespace volcano::render::primitives
