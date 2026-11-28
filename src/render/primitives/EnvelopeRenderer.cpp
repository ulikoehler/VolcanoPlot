// volcano/render/primitives/EnvelopeRenderer.cpp — Vulkan min/max band
#include "volcano/render/primitives/EnvelopeRenderer.hpp"
#include "../VkFactory.hpp"
#include "../VulkanGpuServices.hpp"
#include "../shaders/TransformGlsl.hpp"

#include <volcano/core/Buffer.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/render/VkCmd.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace volcano::render::primitives {

namespace {

/// Hard cap: vkCmdUpdateBuffer takes at most 64 KiB per call, and one
/// record is 16 B. Larger bands fall back to the CPU mesh.
constexpr uint32_t kMaxPoints = 4096;

constexpr const char* kVertHead = R"(
#version 460
layout(set = 0, binding = 0) readonly buffer Pts { vec4 p[]; } pts;

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;  // xy = min, zw = span
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
    vec4 u_color;
} pc;

layout(location = 0) out vec4 v_color;
)";

constexpr const char* kVertMain = R"(
void main() {
    uint i = gl_InstanceIndex;
    vec4 a = pts.p[i];
    vec4 b = pts.p[i + 1u];

    // p = {x, ymin, ymax, pad}; one instanced quad per segment:
    // (upper-left, upper-right, lower-left) / (upper-right, lower-right,
    // lower-left).
    vec2 ul = vec2(a.x, a.z);
    vec2 ur = vec2(b.x, b.z);
    vec2 ll = vec2(a.x, a.y);
    vec2 lr = vec2(b.x, b.y);
    uint k = gl_VertexIndex;
    vec2 p = ul;
    if (k == 1u) p = ur;
    else if (k == 2u) p = ll;
    else if (k == 3u) p = ur;
    else if (k == 4u) p = lr;
    else if (k == 5u) p = ll;

    vec2 q = projFwd(vec2(scaleFwd(p.x, pc.u_scaleX),
                          scaleFwd(p.y, pc.u_scaleY)),
                     pc.u_proj.xyz);
    vec2 ndc = (q - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = pc.u_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() { outColor = v_color; }
)";

[[nodiscard]] inline vk::Rect2D vkScissor(plot::Rect2D r) noexcept {
    return {vk::Offset2D{r.x, r.y}, vk::Extent2D{r.width, r.height}};
}

class EnvelopeRendererVk final : public EnvelopeRenderer {
public:
    explicit EnvelopeRendererVk(VulkanGpuServices& svcs) : svcs_(&svcs) {}

    void init();
    void update(Cmd& cmd, std::span<const float> data) override;
    void draw(Cmd& cmd, plot::Rect2D rect,
              const plot::Transform2D& transform,
              plot::Color color) const override;
    [[nodiscard]] uint32_t pointCount() const noexcept override {
        return pointCount_;
    }

private:
    VulkanGpuServices* svcs_;
    vk::Device device_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::DescriptorPool descPool_;
    vk::DescriptorSet descSet_{};
    core::Buffer points_;
    uint32_t pointCount_ = 0;
};

void EnvelopeRendererVk::init() {
    const vk::Device device = svcs_->device();
    device_ = device;

    auto vertSrc = std::string(kVertHead) + shaders::kScaleFn +
                   shaders::kProjFn + kVertMain;
    auto v = core::ShaderModule::compileGlsl(vertSrc, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::DescriptorSetLayoutBinding binding{};
    binding.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);
    vk::DescriptorSetLayoutCreateInfo dlci{};
    dlci.setBindings(binding);
    descLayout_ = device.createDescriptorSetLayoutUnique(dlci);

    vk::PushConstantRange pcr{};
    pcr.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 20);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descLayout_.get()).setPushConstantRanges(pcr);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    vk::PipelineVertexInputStateCreateInfo visci{};
    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(svcs_->samples());

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR |
                          vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB |
                          vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dsci;
    dsci.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci;
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
        .setRenderPass(svcs_->renderPass())
        .setSubpass(0);

    auto& cache = svcs_->pipelineCache();
    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("Envelope pipeline creation failed");
    pipeline_ = std::move(res.value);

    core::BufferDesc bd{};
    bd.size = vk::DeviceSize(kMaxPoints) * 16;
    bd.usage = core::BufferUsage::Storage;
    points_ = core::Buffer(svcs_->allocator(), bd);

    vk::DescriptorPoolSize ps{};
    ps.setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1);
    descPool_ = core::DescriptorPool(device, {ps}, 1);
    descSet_ = descPool_.allocate(descLayout_.get());
    vk::DescriptorBufferInfo bi{};
    bi.setBuffer(points_.handle()).setOffset(0).setRange(bd.size);
    vk::WriteDescriptorSet w{};
    w.setDstSet(descSet_).setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setBufferInfo(bi);
    device.updateDescriptorSets(w, {});
}

void EnvelopeRendererVk::update(Cmd& cmd, std::span<const float> data) {
    const size_t n = std::min<size_t>(data.size() / 4, kMaxPoints);
    pointCount_ = static_cast<uint32_t>(n);
    if (pointCount_ < 2) return;
    const vk::DeviceSize bytes = vk::DeviceSize(pointCount_) * 16;
    // The pre-pass command buffer orders this after the previous frame
    // and before the draw submission — no CPU/GPU race on the buffer.
    vkCmd(cmd).updateBuffer(points_.handle(), 0, bytes, data.data());
}

void EnvelopeRendererVk::draw(Cmd& cmdRef, plot::Rect2D rect,
                              const plot::Transform2D& transform,
                              plot::Color color) const {
    const auto cmd = vkCmd(cmdRef);
    if (pointCount_ < 2) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float sxCode, sxP1, sxP2, sxPad;
        float syCode, syP1, syP2, syPad;
        float prCode, thetaOff, thetaDir, prPad;
        float cr, cg, cb, ca;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.sxCode = static_cast<float>(static_cast<int>(transform.codeX()));
    pc.sxP1 = transform.scaleX.param1;
    pc.sxP2 = transform.scaleX.param2;
    pc.sxPad = transform.scaleX.param3;
    pc.syCode = static_cast<float>(static_cast<int>(transform.codeY()));
    pc.syP1 = transform.scaleY.param1;
    pc.syP2 = transform.scaleY.param2;
    pc.syPad = transform.scaleY.param3;
    pc.prCode = static_cast<float>(static_cast<int>(transform.projection.kind));
    pc.thetaOff = transform.projection.thetaOffset;
    pc.thetaDir = transform.projection.thetaDir;
    pc.cr = color.r; pc.cg = color.g; pc.cb = color.b; pc.ca = color.a;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           pipelineLayout_.get(), 0, descSet_, {});
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.x))
       .setY(static_cast<float>(rect.y))
       .setWidth(static_cast<float>(rect.width))
       .setHeight(static_cast<float>(rect.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vkScissor(rect));

    cmd.draw(6, pointCount_ - 1, 0, 0);
}

} // namespace

std::unique_ptr<EnvelopeRenderer> makeEnvelopeVk(VulkanGpuServices& svcs) {
    auto p = std::make_unique<EnvelopeRendererVk>(svcs);
    p->init();
    return p;
}

} // namespace volcano::render::primitives
