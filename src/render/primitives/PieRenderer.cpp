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
layout(location = 0) in vec2 a_pos;    // pie vertex in pie data units
layout(location = 1) in vec4 a_color;

layout(push_constant) uniform PC {
    vec2 u_ndcScale;  // NDC units per pie-data unit, per axis
    vec2 u_center;    // mpl `center` in pie data units
} pc;

layout(location = 0) out vec4 v_color;

void main() {
    // a_pos is in matplotlib pie data units (the axes view spans
    // ±1.25, so a radius-1 pie covers 80% of the half-extent). The
    // viewport is the axes rect, so NDC [-1,1] maps to the rect.
    vec2 ndc = (a_pos + pc.u_center) * pc.u_ndcScale;
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
       .setOffset(0).setSize(sizeof(float) * 4);
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
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
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
    center_ = data.center;

    // mpl pie(): x normalized by its sum when `normalize`; fractions of
    // the full circle accumulate from startangle, going counterclockwise
    // (or clockwise when counterclock=False). Vertex space is pie data
    // units: radius = data.radius, explode in the same units.
    const float denom = data.normalize ? total : 1.0f;
    const float dir = data.counterclock ? 1.0f : -1.0f;

    std::vector<plot::Point2D> verts;
    std::vector<plot::Color> colors;
    constexpr int kSeg = 32;

    const size_t n = data.values.size();
    std::vector<float> sliceStart(n), sliceEnd(n);
    float theta1 = data.startAngle / 360.0f;   // circle fractions
    for (size_t i = 0; i < n; ++i) {
        sliceStart[i] = theta1;
        theta1 += dir * (data.values[i] / denom);
        sliceEnd[i] = theta1;
    }

    auto explodeAt = [&](size_t i) {
        if (data.explode.empty()) return 0.0f;
        if (data.explode.size() == 1) return data.explode[0];
        return i < n && i < data.explode.size() ? data.explode[i] : 0.0f;
    };

    // mpl draws wedges in order — later slices paint over earlier ones.
    for (size_t idx = 0; idx < n; ++idx) {
        float sa0 = 2.0f * PI * sliceStart[idx];
        float sa1 = 2.0f * PI * sliceEnd[idx];
        float r0 = data.innerRadius * data.radius;
        float r1 = data.radius;
        // mpl explode: translate the wedge center radially along its
        // bisector by explode (data units).
        float mid = (sa0 + sa1) * 0.5f;
        float expl = explodeAt(idx);
        plot::Point2D off{expl * std::cos(mid), expl * std::sin(mid)};
        plot::Color c = (idx < data.colors.size())
                            ? data.colors[idx]
                            : plot::ColorCycle::at(idx);
        for (int s = 0; s < kSeg; ++s) {
            float ta0 = sa0 + (sa1 - sa0) * s / kSeg;
            float ta1 = sa0 + (sa1 - sa0) * (s + 1) / kSeg;
            plot::Point2D i0{off.x + r0*std::cos(ta0),
                             off.y + r0*std::sin(ta0)};
            plot::Point2D i1{off.x + r0*std::cos(ta1),
                             off.y + r0*std::sin(ta1)};
            plot::Point2D o0{off.x + r1*std::cos(ta0),
                             off.y + r1*std::sin(ta0)};
            plot::Point2D o1{off.x + r1*std::cos(ta1),
                             off.y + r1*std::sin(ta1)};
            verts.insert(verts.end(), {i0, i1, o0, i1, o1, o0});
            for (int j = 0; j < 6; ++j) colors.push_back(c);
        }
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

    // NDC per pie-data unit, per axis. The mpl pie view spans ±1.25,
    // so 1 data unit covers 1/1.25 = 0.8 of the half-extent.
    float halfW = static_cast<float>(rect.extent.width) * 0.5f;
    float halfH = static_cast<float>(rect.extent.height) * 0.5f;
    float halfMin = std::min(halfW, halfH);

    struct PC {
        float ndcScaleX, ndcScaleY;
        float centerX, centerY;
    } pc;
    pc.ndcScaleX = 0.8f * halfMin / halfW;
    pc.ndcScaleY = 0.8f * halfMin / halfH;
    pc.centerX = center_.x;
    pc.centerY = center_.y;

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
