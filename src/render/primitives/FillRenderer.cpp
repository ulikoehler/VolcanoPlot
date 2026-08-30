// volcano/render/primitives/FillRenderer.cpp
#include "volcano/render/primitives/FillRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

// Same vertex shader as BarRenderer: data coords → NDC with log support.
constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;    // data coords
layout(location = 1) in vec4 a_color;  // per-vertex color

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;  // xy = min, zw = span
    vec2 u_log;
} pc;

layout(location = 0) out vec4 v_color;

vec2 applyLog(vec2 p) {
    if (pc.u_log.x > 0.5) p.x = log(max(p.x,1e-30)) / log(10.0);
    if (pc.u_log.y > 0.5) p.y = log(max(p.y,1e-30)) / log(10.0);
    return p;
}

void main() {
    vec2 p = applyLog(a_pos);
    vec2 ndc = (p - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() { outColor = v_color; }
)";

} // namespace

void FillRenderer::init(vk::Device device, vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples,
                        core::PipelineCache& cache) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 6);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    // Two bindings: position (vec2) + color (vec4)
    vk::VertexInputBindingDescription bindings[2] = {
        {0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex},
        {1, sizeof(plot::Color),   vk::VertexInputRate::eVertex},
    };
    vk::VertexInputAttributeDescription attrs[2] = {
        {0, 0, vk::Format::eR32G32Sfloat,      0},
        {1, 1, vk::Format::eR32G32B32A32Sfloat, 0},
    };
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingDescriptions(bindings).setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    // Alpha blending (same as BarRenderer).
    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOne)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
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
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess)
        throw std::runtime_error("Fill pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void FillRenderer::upload(vk::Device device, vk::Queue queue,
                          vk::CommandPool pool, VmaAllocator allocator,
                          std::span<const plot::Point2D> positions,
                          std::span<const plot::Color> colors) {
    vertexCount_ = static_cast<uint32_t>(positions.size());
    if (vertexCount_ == 0) return;

    core::BufferDesc pdesc;
    pdesc.size = positions.size_bytes();
    pdesc.usage = core::BufferUsage::VertexStorage;
    posBuffer_ = core::Buffer(allocator, pdesc);
    posBuffer_.upload(device, queue, pool,
                      std::as_bytes(positions));

    core::BufferDesc cdesc;
    cdesc.size = colors.size_bytes();
    cdesc.usage = core::BufferUsage::Vertex;
    colorBuffer_ = core::Buffer(allocator, cdesc);
    colorBuffer_.upload(device, queue, pool,
                        std::as_bytes(colors));
}

void FillRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        const plot::Transform2D& transform) const {
    if (!inited_ || vertexCount_ == 0) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float logX, logY;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.logX = transform.logX ? 1.0f : 0.0f;
    pc.logY = transform.logY ? 1.0f : 0.0f;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.offset.x))
       .setY(static_cast<float>(rect.offset.y))
       .setWidth(static_cast<float>(rect.extent.width))
       .setHeight(static_cast<float>(rect.extent.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 2> buf = {posBuffer_.handle(), colorBuffer_.handle()};
    std::array<vk::DeviceSize, 2> off = {0, 0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(vertexCount_, 1, 0, 0);
}

} // namespace volcano::render::primitives
