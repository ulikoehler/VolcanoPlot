// volcano/render/primitives/LineSegmentRenderer.cpp
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

// Same vertex shader as LineRenderer: data coords → NDC with log support.
constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;
    vec4 u_rect;
    vec4 u_color;
    vec2 u_log;
    float u_width;
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
    v_color = pc.u_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() { outColor = v_color; }
)";

} // namespace

void LineSegmentRenderer::init(vk::Device device, vk::RenderPass renderPass,
                                vk::SampleCountFlagBits samples,
                                core::PipelineCache& cache) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 15);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bind{0, sizeof(plot::Point2D),
                                            vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attr);

    // Key difference: eLineList instead of eLineStrip.
    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eLineList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.lineWidth = 2.0f;
    rsci.polygonMode = vk::PolygonMode::eFill;
    rsci.cullMode = vk::CullModeFlagBits::eNone;

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

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
        throw std::runtime_error("LineSegment pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void LineSegmentRenderer::upload(vk::Device device, vk::Queue queue,
                                  vk::CommandPool pool, VmaAllocator allocator,
                                  std::span<const plot::Point2D> points,
                                  plot::Color color, float width) {
    core::BufferDesc d;
    d.size = points.size_bytes();
    d.usage = core::BufferUsage::VertexStorage;
    pointBuffer_ = core::Buffer(allocator, d);
    pointBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{points.data(), points.size()}));
    color_ = color;
    width_ = width;
    count_ = static_cast<uint32_t>(points.size());
}

void LineSegmentRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                                const plot::Transform2D& transform,
                                uint32_t vertexCount) const {
    if (!inited_ || vertexCount < 2) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float rectX, rectY, rectW, rectH;
        float r, g, b, a;
        float logX, logY;
        float width;
    } pc{};
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.rectX = static_cast<float>(rect.offset.x);
    pc.rectY = static_cast<float>(rect.offset.y);
    pc.rectW = static_cast<float>(rect.extent.width);
    pc.rectH = static_cast<float>(rect.extent.height);
    pc.r = color_.r; pc.g = color_.g; pc.b = color_.b; pc.a = color_.a;
    pc.logX = transform.logX ? 1.0f : 0.0f;
    pc.logY = transform.logY ? 1.0f : 0.0f;
    pc.width = width_;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.pushConstants(pipelineLayout_.get(),
                      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PC), &pc);
    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.offset.x))
       .setY(static_cast<float>(rect.offset.y))
       .setWidth(static_cast<float>(rect.extent.width))
       .setHeight(static_cast<float>(rect.extent.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 1> buf = {pointBuffer_.handle()};
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(vertexCount, 1, 0, 0);
}

} // namespace volcano::render::primitives
