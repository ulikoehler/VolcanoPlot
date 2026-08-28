// volcano/render/GridRenderer.cpp
#include "volcano/render/GridRenderer.hpp"
#include <stdexcept>

namespace volcano::render {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos; // fullscreen quad [-1,1]
layout(location = 0) out vec2 v_fb;
layout(push_constant) uniform PC {
    vec4 u_rect; // xy = offset, zw = extent
} pc;
void main() {
    vec2 fb = pc.u_rect.xy + (a_pos * 0.5 + 0.5) * pc.u_rect.zw;
    v_fb = fb;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
#extension GL_OES_standard_derivatives : enable
layout(location = 0) in vec2 v_fb;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 u_rect;
    vec4 u_viewMinSpanX; // xy = min, z = span, w = logX
    vec4 u_viewMinSpanY; // xy = min, z = span, w = logY
    vec4 u_gridColorX;   // rgb + alpha
    vec4 u_gridColorY;
    vec2 u_gridPxPerWorld; // x,y
} pc;

float gridLineDistPx(float base, float delta, float step, float pxPerWorld) {
    if (step <= 0.0) return 1e9;
    float baseFrac = fract(base / step);
    float p = baseFrac + delta / step;
    float fp = fract(p);
    float dWorld = min(fp, 1.0 - fp);
    return dWorld * pxPerWorld;
}

void main() {
    // Convert framebuffer pixel to data coords.
    vec2 ndc = (v_fb - pc.u_rect.xy) / pc.u_rect.zw * 2.0 - 1.0;
    vec2 data = pc.u_viewMinSpanX.xy + (ndc * 0.5 + 0.5) * pc.u_viewMinSpanX.zw;
    // Log scale
    if (pc.u_viewMinSpanX.w > 0.5) data.x = log(max(data.x, 1e-30)) / log(10.0);
    if (pc.u_viewMinSpanY.w > 0.5) data.y = log(max(data.y, 1e-30)) / log(10.0);

    // Compute nice tick step (1, 2, 5 × 10^n).
    float stepX = pow(10.0, floor(log(abs(pc.u_viewMinSpanX.z)) / log(10.0)));
    float stepY = pow(10.0, floor(log(abs(pc.u_viewMinSpanY.z)) / log(10.0)));

    float dx = gridLineDistPx(pc.u_viewMinSpanX.x, data.x - pc.u_viewMinSpanX.x, stepX, pc.u_gridPxPerWorld.x);
    float dy = gridLineDistPx(pc.u_viewMinSpanY.x, data.y - pc.u_viewMinSpanY.x, stepY, pc.u_gridPxPerWorld.y);

    float aX = smoothstep(1.0, 0.0, dx);
    float aY = smoothstep(1.0, 0.0, dy);
    vec3 c = pc.u_gridColorX.rgb * aX + pc.u_gridColorY.rgb * aY;
    float a = max(aX * pc.u_gridColorX.a, aY * pc.u_gridColorY.a);
    outColor = vec4(c, a);
}
)";

} // namespace

void GridRenderer::init(vk::Device device, vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 24);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    // Fullscreen triangle
    vk::VertexInputBindingDescription bind{0, sizeof(float)*2, vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingStates(bind).setVertexAttributeStates(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci;
    rsci.setPolygonMode(vk::PolygonMode::eFill).setCullMode(vk::CullModeFlagBits::eNone);

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
    gpci.setStages(stages).setPVertexInputState(&visci).setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci).setPRasterizationState(&rsci).setPMultisampleState(&msci)
        .setPColorBlendState(&cbsci).setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get()).setRenderPass(renderPass).setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Grid pipeline failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void GridRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        const plot::Transform2D& transform,
                        const plot::AxisStyle& xAxis, const plot::AxisStyle& yAxis) const {
    if (!inited_) return;
    // Fullscreen triangle vertices.
    static const float verts[] = { -1,-1, 3,-1, -1,3 };
    // (In a real impl, upload these once to a static buffer.)

    struct PC {
        float rectX, rectY, rectW, rectH;
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float logX, logY;
        float xR, xG, xB, xA;
        float yR, yG, yB, yA;
        float pxPerWorldX, pxPerWorldY;
    } pc{};
    pc.rectX = static_cast<float>(rect.offset.x);
    pc.rectY = static_cast<float>(rect.offset.y);
    pc.rectW = static_cast<float>(rect.extent.width);
    pc.rectH = static_cast<float>(rect.extent.height);
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.logX = transform.logX ? 1.0f : 0.0f;
    pc.logY = transform.logY ? 1.0f : 0.0f;
    pc.xR = xAxis.gridColor.r; pc.xG = xAxis.gridColor.g; pc.xB = xAxis.gridColor.b; pc.xA = xAxis.gridColor.a;
    pc.yR = yAxis.gridColor.r; pc.yG = yAxis.gridColor.g; pc.yB = yAxis.gridColor.b; pc.yA = yAxis.gridColor.a;
    pc.pxPerWorldX = rect.extent.width / std::max(transform.view.x.span(), 1e-30f);
    pc.pxPerWorldY = rect.extent.height / std::max(transform.view.y.span(), 1e-30f);

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
    // TODO: bind static fullscreen vertex buffer.
    // For now, draw with no VBO using a static buffer created in init().
    // cmd.draw(3, 1, 0, 0);
    (void)verts;
}

} // namespace volcano::render
