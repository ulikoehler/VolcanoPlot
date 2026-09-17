// volcano/render/GridRenderer.cpp
#include "volcano/render/GridRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <array>
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
    vec4 u_viewMinSpan;  // xy = min, zw = span (display space)
    vec4 u_gridColorX;   // major rgb + alpha
    vec4 u_gridColorY;
    vec4 u_minorColorX;  // minor rgb + alpha
    vec4 u_minorColorY;
    vec2 u_gridPxPerWorld; // x,y
    vec2 u_minorDiv;     // subdivisions per major step (x,y)
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
    // Convert framebuffer pixel to display coords (view is already
    // scale-transformed; for log axes this is log10 space so a step of
    // 1 produces decade lines).
    vec2 ndc = (v_fb - pc.u_rect.xy) / pc.u_rect.zw * 2.0 - 1.0;
    vec2 data = pc.u_viewMinSpan.xy + (ndc * 0.5 + 0.5) * pc.u_viewMinSpan.zw;

    // Compute nice tick step (1, 2, 5 × 10^n) in display space.
    float stepX = pow(10.0, floor(log(abs(pc.u_viewMinSpan.z)) / log(10.0)));
    float stepY = pow(10.0, floor(log(abs(pc.u_viewMinSpan.w)) / log(10.0)));

    float dx = gridLineDistPx(pc.u_viewMinSpan.x, data.x - pc.u_viewMinSpan.x, stepX, pc.u_gridPxPerWorld.x);
    float dy = gridLineDistPx(pc.u_viewMinSpan.y, data.y - pc.u_viewMinSpan.y, stepY, pc.u_gridPxPerWorld.y);

    float aX = smoothstep(1.0, 0.0, dx);
    float aY = smoothstep(1.0, 0.0, dy);
    vec3 cMaj = pc.u_gridColorX.rgb * aX + pc.u_gridColorY.rgb * aY;
    float aMaj = max(aX * pc.u_gridColorX.a, aY * pc.u_gridColorY.a);

    // Minor grid: subdivisions of the major step (auto minor locator).
    float mdx = gridLineDistPx(pc.u_viewMinSpan.x, data.x - pc.u_viewMinSpan.x,
                               stepX / pc.u_minorDiv.x, pc.u_gridPxPerWorld.x);
    float mdy = gridLineDistPx(pc.u_viewMinSpan.y, data.y - pc.u_viewMinSpan.y,
                               stepY / pc.u_minorDiv.y, pc.u_gridPxPerWorld.y);
    float amX = smoothstep(0.8, 0.0, mdx);
    float amY = smoothstep(0.8, 0.0, mdy);
    vec3 cMin = pc.u_minorColorX.rgb * amX + pc.u_minorColorY.rgb * amY;
    float aMin = max(amX * pc.u_minorColorX.a, amY * pc.u_minorColorY.a);

    // Composite major lines over minor lines.
    vec3 c = cMaj + cMin * (1.0 - aMaj);
    float a = aMaj + aMin * (1.0 - aMaj);
    outColor = vec4(c, a);
}
)";

} // namespace

void GridRenderer::init(vk::Device device, vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples, core::PipelineCache& cache,
                        VmaAllocator allocator, vk::Queue queue, vk::CommandPool pool) {
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    // Fullscreen triangle vertex buffer (NDC coords).
    static const float verts[] = { -1,-1, 3,-1, -1,3 };
    core::BufferDesc bdesc{};
    bdesc.size = sizeof(verts);
    bdesc.usage = core::BufferUsage::Vertex;
    fullscreenBuffer_ = core::Buffer(allocator, bdesc);
    fullscreenBuffer_.upload(device, queue, pool,
                              std::as_bytes(std::span{verts, 3}));

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 28);
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
    visci.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill).setCullMode(vk::CullModeFlagBits::eNone);

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
    gpci.setStages(stages).setPVertexInputState(&visci).setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci).setPRasterizationState(&rsci).setPMultisampleState(&msci)
        .setPDepthStencilState(&depthState)
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

    // gridWhich: "major" → major only, "minor" → minor only, "both" → both.
    // Axis `grid` flag gates that axis's lines entirely (grid(axis="x")).
    auto whichMajor = [](const plot::AxisStyle& a) {
        return a.grid && a.gridWhich != "minor";
    };
    auto whichMinor = [](const plot::AxisStyle& a) {
        return a.grid && (a.gridWhich == "minor" || a.gridWhich == "both");
    };

    struct PC {
        float rectX, rectY, rectW, rectH;
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float xR, xG, xB, xA;
        float yR, yG, yB, yA;
        float mxR, mxG, mxB, mxA;
        float myR, myG, myB, myA;
        float pxPerWorldX, pxPerWorldY;
        float minorDivX, minorDivY;
    } pc{};
    pc.rectX = static_cast<float>(rect.offset.x);
    pc.rectY = static_cast<float>(rect.offset.y);
    pc.rectW = static_cast<float>(rect.extent.width);
    pc.rectH = static_cast<float>(rect.extent.height);
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.xR = xAxis.gridColor.r; pc.xG = xAxis.gridColor.g; pc.xB = xAxis.gridColor.b;
    pc.xA = xAxis.gridColor.a * (whichMajor(xAxis) ? 1.0f : 0.0f);
    pc.yR = yAxis.gridColor.r; pc.yG = yAxis.gridColor.g; pc.yB = yAxis.gridColor.b;
    pc.yA = yAxis.gridColor.a * (whichMajor(yAxis) ? 1.0f : 0.0f);
    pc.mxR = xAxis.minorGridColor.r; pc.mxG = xAxis.minorGridColor.g;
    pc.mxB = xAxis.minorGridColor.b;
    pc.mxA = xAxis.minorGridColor.a * (whichMinor(xAxis) ? 1.0f : 0.0f);
    pc.myR = yAxis.minorGridColor.r; pc.myG = yAxis.minorGridColor.g;
    pc.myB = yAxis.minorGridColor.b;
    pc.myA = yAxis.minorGridColor.a * (whichMinor(yAxis) ? 1.0f : 0.0f);
    pc.pxPerWorldX = rect.extent.width / std::max(transform.view.x.span(), 1e-30f);
    pc.pxPerWorldY = rect.extent.height / std::max(transform.view.y.span(), 1e-30f);
    pc.minorDivX = pc.minorDivY = 5.0f; // AutoMinorLocator default

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

    std::array<vk::Buffer, 1> buf = { fullscreenBuffer_.handle() };
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(3, 1, 0, 0);
}

} // namespace volcano::render
