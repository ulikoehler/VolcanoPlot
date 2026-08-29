// volcano/render/primitives/PieRenderer.cpp
#include "volcano/render/primitives/PieRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <cmath>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

constexpr float PI = 3.14159265358979323846f;

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;    // pie vertex in [-1,1] circle space
layout(location = 1) in vec4 a_color;

layout(push_constant) uniform PC {
    vec4 u_rect;     // xy = center px, zw = half-extent px
    float u_innerR;  // inner radius (donut)
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    // a_pos is in unit circle [-1,1] space. The viewport maps NDC [-1,1]
    // to the rect pixels, so we output a_pos directly as NDC. The pie
    // fills the rect; for non-square rects it would be elliptical, but
    // the draw() function sets halfW=halfH=min(w,h)*0.45 so the geometry
    // is already circular — we just need to position it at center.
    // NDC of center = (center - rect_origin - rect_extent/2) / (rect_extent/2)
    // But since viewport = rect, NDC [-1,1] maps to rect. Center of rect
    // in NDC = (0,0). So we just scale a_pos by the ratio of pie radius
    // to rect half-extent.
    vec2 ndc = a_pos;  // unit circle -> NDC (fills rect)
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

void PieRenderer::init(vk::Device device, vk::RenderPass renderPass,
                       vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 5);
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bindings[2] = {
        {0, sizeof(plot::Point2D), vk::VertexInputRate::eVertex},
        {1, sizeof(plot::Color),   vk::VertexInputRate::eVertex},
    };
    vk::VertexInputAttributeDescription attrs[2] = {
        {0, 0, vk::Format::eR32G32Sfloat,       0},
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
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Pie pipeline creation failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void PieRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                         VmaAllocator allocator, const plot::PieData& data) {
    float total = 0;
    for (auto v : data.values) total += v;
    if (total <= 0) return;

    std::vector<plot::Point2D> verts;
    std::vector<plot::Color> colors;
    constexpr int kSeg = 32;

    float a0 = 0;
    for (size_t i = 0; i < data.values.size(); ++i) {
        float a1 = a0 + 2.0f * PI * data.values[i] / total;
        float r0 = data.innerRadius;
        float r1 = 1.0f + data.explode * float(i);
        plot::Color c = (i < data.colors.size()) ? data.colors[i]
                                                  : plot::Color::fromRgba8(31, 119, 180);
        for (int s = 0; s < kSeg; ++s) {
            float ta0 = a0 + (a1 - a0) * s / kSeg;
            float ta1 = a0 + (a1 - a0) * (s + 1) / kSeg;
            plot::Point2D i0{r0*std::cos(ta0), r0*std::sin(ta0)};
            plot::Point2D i1{r0*std::cos(ta1), r0*std::sin(ta1)};
            plot::Point2D o0{r1*std::cos(ta0), r1*std::sin(ta0)};
            plot::Point2D o1{r1*std::cos(ta1), r1*std::sin(ta1)};
            verts.insert(verts.end(), {i0, i1, o0, i1, o1, o0});
            for (int j = 0; j < 6; ++j) colors.push_back(c);
        }
        a0 = a1;
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

void PieRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect) const {
    if (!inited_ || vertexCount_ == 0) return;

    struct PC {
        float centerX, centerY, halfW, halfH;
        float innerR;
    } pc;
    pc.centerX = static_cast<float>(rect.offset.x + rect.extent.width / 2);
    pc.centerY = static_cast<float>(rect.offset.y + rect.extent.height / 2);
    float half = std::min(static_cast<float>(rect.extent.width),
                          static_cast<float>(rect.extent.height)) * 0.45f;
    pc.halfW = half;
    pc.halfH = half;
    pc.innerR = 0.0f;  // set from data if needed

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
