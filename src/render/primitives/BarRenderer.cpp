// volcano/render/primitives/BarRenderer.cpp
#include "volcano/render/primitives/BarRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include "../shaders/TransformGlsl.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertHead = R"(
#version 460
layout(location = 0) in vec2 a_pos;    // data coords (bar corner)
layout(location = 1) in vec4 a_color;  // per-bar color

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;  // xy = min, zw = span
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
} pc;

layout(location = 0) out vec4 v_color;
)";

constexpr const char* kVertMain = R"(
void main() {
    vec2 p = projFwd(vec2(scaleFwd(a_pos.x, pc.u_scaleX.xyz),
                          scaleFwd(a_pos.y, pc.u_scaleY.xyz)),
                     pc.u_proj.xyz);
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

void BarRenderer::init(vk::Device device, vk::RenderPass renderPass,
                       vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    device_ = device;
    auto vertSrc = std::string(kVertHead) + shaders::kScaleFn +
                   shaders::kProjFn + kVertMain;
    auto v = core::ShaderModule::compileGlsl(vertSrc, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 16);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    // Two bindings: position (vec2) + color (vec4)
    vk::VertexInputBindingDescription bindings[2] = {
        {0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex},
        {1, sizeof(plot::Color),   vk::VertexInputRate::eVertex},
    };
    vk::VertexInputAttributeDescription attrs[2] = {
        {0, 0, vk::Format::eR32G32Sfloat,     0},
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

    // Depth testing disabled (2D overlay).
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
        .setPDepthStencilState(&depthState)
        .setPColorBlendState(&cbsci)
        .setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get())
        .setRenderPass(renderPass)
        .setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Bar pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void BarRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                         VmaAllocator allocator, const plot::BarData& data) {
    // Build a quad per bar: 6 vertices (two triangles).
    std::vector<plot::Point2D> verts;
    std::vector<plot::Color> colors;
    verts.reserve(data.heights.size() * 6);
    colors.reserve(data.heights.size() * 6);
    float n = static_cast<float>(data.heights.size());
    float bw = data.width / n;
    for (size_t i = 0; i < data.heights.size(); ++i) {
        float x0 = i * bw + (1.0f - data.width) * 0.5f;
        float x1 = x0 + bw;
        float h = data.heights[i];
        plot::Point2D bl{x0, 0}, br{x1, 0}, tl{x0, h}, tr{x1, h};
        verts.insert(verts.end(), {bl, br, tl, br, tr, tl});
        plot::Color c = (i < data.colors.size()) ? data.colors[i]
                                                  : plot::Color::fromRgba8(31, 119, 180);
        for (int j = 0; j < 6; ++j) colors.push_back(c);
    }
    vertexCount_ = static_cast<uint32_t>(verts.size());

    core::BufferDesc pdesc;
    pdesc.size = verts.size() * sizeof(plot::Point2D);
    pdesc.usage = core::BufferUsage::Vertex;
    posBuffer_ = core::Buffer(allocator, pdesc);
    posBuffer_.upload(device, queue, pool,
                      std::as_bytes(std::span{verts.data(), verts.size()}));

    core::BufferDesc cdesc;
    cdesc.size = colors.size() * sizeof(plot::Color);
    cdesc.usage = core::BufferUsage::Vertex;
    colorBuffer_ = core::Buffer(allocator, cdesc);
    colorBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{colors.data(), colors.size()}));
}

void BarRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                       const plot::Transform2D& transform) const {
    if (!inited_ || vertexCount_ == 0) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float sxCode, sxP1, sxP2, sxPad;
        float syCode, syP1, syP2, syPad;
        float prCode, thetaOff, thetaDir, prPad;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.sxCode = static_cast<float>(static_cast<int>(transform.codeX()));
    pc.sxP1 = transform.scaleX.param1;
    pc.sxP2 = transform.scaleX.param2;
    pc.syCode = static_cast<float>(static_cast<int>(transform.codeY()));
    pc.syP1 = transform.scaleY.param1;
    pc.syP2 = transform.scaleY.param2;
    pc.prCode = static_cast<float>(static_cast<int>(transform.projection.kind));
    pc.thetaOff = transform.projection.thetaOffset;
    pc.thetaDir = transform.projection.thetaDir;

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

    std::array<vk::Buffer, 2> buf = { posBuffer_.handle(), colorBuffer_.handle() };
    std::array<vk::DeviceSize, 2> off = {0, 0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(vertexCount_, 1, 0, 0);
}

} // namespace volcano::render::primitives
