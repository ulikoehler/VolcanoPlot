// volcano/render/primitives/LineRenderer.cpp
#include "volcano/render/primitives/LineRenderer.hpp"
#include <volcano/plot/Transform.hpp>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;
    vec4 u_rect;
    vec2 u_log;
    vec4 u_color;
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
    vec2 ndc = (p - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw;
    vec2 fb = pc.u_rect.xy + (ndc * 0.5 + 0.5) * pc.u_rect.zw;
    vec2 clip = vec2(fb.x / pc.u_rect.z * 2.0 - 1.0, fb.y / pc.u_rect.w * -2.0 + 1.0);
    gl_Position = vec4(clip, 0.0, 1.0);
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

void LineRenderer::init(vk::Device device, vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 13);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bind{0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingStates(bind).setVertexAttributeStates(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eLineStrip);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci;
    rsci.setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci;
    dsci.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci;
    gpci.setStages(stages)
        .setPVertexInputState(&visci)
        .setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci)
        .setPRasterizationState(&rsci)
        .setPMultisampleState(&msci)
        .setPColorBlendState(&cbsci)
        .setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get())
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Line pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void LineRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                          VmaAllocator allocator, std::span<const plot::Point2D> points,
                          plot::Color /*color*/, float /*width*/) {
    core::BufferDesc d;
    d.size = points.size_bytes();
    d.usage = core::BufferUsage::Vertex;
    pointBuffer_ = core::Buffer(allocator, d);
    pointBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{points.data(), points.size()}));
}

void LineRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        const plot::Transform2D& transform, uint32_t pointCount) const {
    if (!inited_ || pointCount < 2) return;
    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float rectX, rectY, rectW, rectH;
        float logX, logY;
        float r, g, b, a;
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
    pc.logX = transform.logX ? 1.0f : 0.0f;
    pc.logY = transform.logY ? 1.0f : 0.0f;

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

    vk::Buffer buf = pointBuffer_.handle();
    vk::DeviceSize off = 0;
    cmd.bindVertexBuffers(0, 1, &buf, &off);
    cmd.draw(pointCount, 1, 0, 0);
}

} // namespace volcano::render::primitives
