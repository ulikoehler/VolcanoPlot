// volcano/render/primitives/PointRenderer.cpp
#include "volcano/render/primitives/PointRenderer.hpp"
#include "volcano/core/Device.hpp"
#include "volcano/core/DescriptorPool.hpp"

#include <volcano/plot/Transform.hpp>

#include <array>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

// Embedded SPIR-V source (compiled at runtime via shaderc if available,
// otherwise loaded from precompiled .spv files).
constexpr const char* kVertGlsl = R"(
#version 460

layout(location = 0) in vec2 a_pos;       // data coords
layout(location = 1) in vec4 a_color;     // RGBA
layout(location = 2) in float a_size;     // pixel size

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;   // xy = min, zw = span
    vec4 u_rect;          // xy = offset, zw = extent (for point size scaling)
    vec2 u_log;           // x = logX, y = logY
};

layout(location = 0) out vec4 v_color;
layout(location = 1) out float v_size;

vec2 applyLog(vec2 p) {
    if (u_log.x > 0.5) p.x = log(p.x) / log(10.0);
    if (u_log.y > 0.5) p.y = log(p.y) / log(10.0);
    return p;
}

void main() {
    vec2 p = applyLog(a_pos);
    // Map data coords to NDC [-1,1] — viewport handles pixel mapping.
    vec2 ndc = (p - u_viewMinSpan.xy) / u_viewMinSpan.zw * 2.0 - 1.0;
    // Vulkan Y is down, flip to conventional math Y-up.
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    gl_PointSize = a_size;
    v_color = a_color;
    v_size = a_size;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 1) in float v_size;
layout(location = 0) out vec4 outColor;

void main() {
    // Circular marker via gl_PointCoord
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float r = dot(c, c);
    if (r > 1.0) discard;
    // Anti-alias edge
    float alpha = smoothstep(1.0, 0.9, r);
    outColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

} // namespace

void PointRenderer::init(vk::Device device, vk::RenderPass renderPass,
                         vk::SampleCountFlagBits samples, core::DescriptorPool& /*descPool*/,
                         core::PipelineCache& cache) {
    device_ = device;
    auto vertSpv = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, vertSpv);
    frag_ = core::ShaderModule(device, fragSpv);

    vk::PipelineLayoutCreateInfo plci{};
    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 10);
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription binding{};
    binding.setBinding(0).setStride(sizeof(plot::Point2D)).setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription attrs[3];
    attrs[0].setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    attrs[1].setLocation(1).setBinding(1).setFormat(vk::Format::eR32G32B32A32Sfloat).setOffset(0);
    attrs[2].setLocation(2).setBinding(2).setFormat(vk::Format::eR32Sfloat).setOffset(0);

    vk::VertexInputBindingDescription bindings[3] = {
        {0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex},
        {1, sizeof(plot::Color),   vk::VertexInputRate::eVertex},
        {2, sizeof(float),         vk::VertexInputRate::eVertex},
    };

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(bindings).setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::ePointList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples)
        .setSampleShadingEnable(false);

    // Depth testing disabled (2D overlay).
    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att{};
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorBlendOp(vk::BlendOp::eAdd)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setAlphaBlendOp(vk::BlendOp::eAdd)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendStateCreateInfo cbsci{};
    cbsci.setLogicOpEnable(false).setAttachments(att);

    std::vector<vk::DynamicState> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci{};
    dsci.setDynamicStates(dyn);

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
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Failed to create point pipeline");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void PointRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                           VmaAllocator allocator, std::span<const plot::Point2D> points,
                           std::span<const plot::Color> colors, std::span<const float> sizes) {
    core::BufferDesc pdesc{};
    pdesc.size = points.size_bytes();
    pdesc.usage = core::BufferUsage::Vertex;
    pointBuffer_ = core::Buffer(allocator, pdesc);
    pointBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{points.data(), points.size()}));

    core::BufferDesc cdesc{};
    cdesc.size = colors.size_bytes();
    cdesc.usage = core::BufferUsage::Vertex;
    colorBuffer_ = core::Buffer(allocator, cdesc);
    colorBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{colors.data(), colors.size()}));

    core::BufferDesc sdesc{};
    sdesc.size = sizes.size_bytes();
    sdesc.usage = core::BufferUsage::Vertex;
    sizeBuffer_ = core::Buffer(allocator, sdesc);
    sizeBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{sizes.data(), sizes.size()}));
}

void PointRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                         const plot::Transform2D& transform, uint32_t pointCount) const {
    if (!inited_ || pointCount == 0) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float rectX, rectY, rectW, rectH;
        float logX, logY;
    } pc;
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
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(PC), &pc);

    vk::Viewport vp;
    vp.setX(static_cast<float>(rect.offset.x))
       .setY(static_cast<float>(rect.offset.y))
       .setWidth(static_cast<float>(rect.extent.width))
       .setHeight(static_cast<float>(rect.extent.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 3> buffers = { pointBuffer_.handle(), colorBuffer_.handle(), sizeBuffer_.handle() };
    std::array<vk::DeviceSize, 3> offsets = {0,0,0};
    cmd.bindVertexBuffers(0, buffers, offsets);
    cmd.draw(pointCount, 1, 0, 0);
}

} // namespace volcano::render::primitives
