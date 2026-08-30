// volcano/render/primitives/SurfaceRenderer.cpp
#include "volcano/render/primitives/SurfaceRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec3 a_pos;  // x, y, z (data coords, z = value)

layout(push_constant) uniform PC {
    mat4 u_vp;          // view-projection matrix
    vec4 u_gridRange;   // xy = xRange min/max, zw = yRange min/max
    vec2 u_valueRange;  // min, max of z values
} pc;

layout(location = 0) out float v_height;

void main() {
    // Normalize z to [0,1] for color.
    v_height = (a_pos.z - pc.u_valueRange.x) / max(pc.u_valueRange.y - pc.u_valueRange.x, 1e-30);
    gl_Position = pc.u_vp * vec4(a_pos, 1.0);
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in float v_height;
layout(location = 0) out vec4 outColor;

vec3 viridis(float t) {
    // Approximate viridis colormap.
    vec3 c0 = vec3(0.267, 0.005, 0.329);
    vec3 c1 = vec3(0.282, 0.140, 0.457);
    vec3 c2 = vec3(0.254, 0.265, 0.530);
    vec3 c3 = vec3(0.207, 0.372, 0.553);
    vec3 c4 = vec3(0.164, 0.471, 0.558);
    vec3 c5 = vec3(0.128, 0.567, 0.551);
    vec3 c6 = vec3(0.135, 0.659, 0.518);
    vec3 c7 = vec3(0.267, 0.749, 0.441);
    vec3 c8 = vec3(0.478, 0.821, 0.318);
    vec3 c9 = vec3(0.741, 0.873, 0.150);
    vec3 c10 = vec3(0.993, 0.906, 0.144);
    float s = t * 10.0;
    int i = int(s);
    float f = s - float(i);
    if (i == 0)  return mix(c0, c1, f);
    if (i == 1)  return mix(c1, c2, f);
    if (i == 2)  return mix(c2, c3, f);
    if (i == 3)  return mix(c3, c4, f);
    if (i == 4)  return mix(c4, c5, f);
    if (i == 5)  return mix(c5, c6, f);
    if (i == 6)  return mix(c6, c7, f);
    if (i == 7)  return mix(c7, c8, f);
    if (i == 8)  return mix(c8, c9, f);
    return mix(c9, c10, f);
}

void main() {
    vec3 color = viridis(clamp(v_height, 0.0, 1.0));
    outColor = vec4(color, 1.0);
}
)";

} // namespace

void SurfaceRenderer::init(vk::Device device, vk::RenderPass renderPass,
                           vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * (16 + 4 + 2));
    vk::PipelineLayoutCreateInfo plci;
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription bind{0, sizeof(plot::Point3D), vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attr{0, 0, vk::Format::eR32G32B32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo visci;
    visci.setVertexBindingDescriptions(bind).setVertexAttributeDescriptions(attr);

    vk::PipelineInputAssemblyStateCreateInfo iaci;
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci;
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eClockwise);

    // Depth testing enabled — render pass now has a depth attachment.
    vk::PipelineDepthStencilStateCreateInfo dsci;
    dsci.setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess)
        .setDepthBoundsTestEnable(false)
        .setStencilTestEnable(false);

    vk::PipelineMultisampleStateCreateInfo msci;
    msci.setRasterizationSamples(samples);

    vk::PipelineColorBlendAttachmentState att;
    att.setBlendEnable(false)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci;
    cbsci.setAttachments(att);

    std::vector<vk::DynamicState> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci2;
    dsci2.setDynamicStates(dyn);

    vk::GraphicsPipelineCreateInfo gpci;
    gpci.setStages(stages).setPVertexInputState(&visci).setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci).setPRasterizationState(&rsci).setPMultisampleState(&msci)
        .setPDepthStencilState(&dsci).setPColorBlendState(&cbsci).setPDynamicState(&dsci2)
        .setLayout(pipelineLayout_.get()).setRenderPass(renderPass).setSubpass(0);

    auto res = device.createGraphicsPipelineUnique(cache.handle(), gpci);
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Surface pipeline failed");
    pipeline_ = std::move(res.value);
    inited_ = true;
}

void SurfaceRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                             VmaAllocator allocator, const plot::Grid2D& grid) {
    // Build vertex grid: (width × height) vertices, each with (x, y, z=value).
    std::vector<plot::Point3D> verts(grid.width * grid.height);
    float xMin = grid.xRange.min, xMax = grid.xRange.max;
    float yMin = grid.yRange.min, yMax = grid.yRange.max;
    for (uint32_t j = 0; j < grid.height; ++j) {
        for (uint32_t i = 0; i < grid.width; ++i) {
            float x = xMin + float(i) / float(grid.width - 1) * (xMax - xMin);
            float y = yMin + float(j) / float(grid.height - 1) * (yMax - yMin);
            float z = grid.values[j * grid.width + i];
            verts[j * grid.width + i] = {x, y, z};
        }
    }

    // Build index list: two triangles per quad.
    std::vector<uint32_t> indices;
    indices.reserve((grid.width - 1) * (grid.height - 1) * 6);
    for (uint32_t j = 0; j < grid.height - 1; ++j) {
        for (uint32_t i = 0; i < grid.width - 1; ++i) {
            uint32_t a = j * grid.width + i;
            uint32_t b = a + 1;
            uint32_t c = a + grid.width;
            uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
    indexCount_ = static_cast<uint32_t>(indices.size());

    core::BufferDesc vdesc;
    vdesc.size = verts.size() * sizeof(plot::Point3D);
    vdesc.usage = core::BufferUsage::Vertex;
    vertexBuffer_ = core::Buffer(allocator, vdesc);
    vertexBuffer_.upload(device, queue, pool,
                         std::as_bytes(std::span{verts.data(), verts.size()}));

    core::BufferDesc idesc;
    idesc.size = indices.size() * sizeof(uint32_t);
    idesc.usage = core::BufferUsage::Index;
    indexBuffer_ = core::Buffer(allocator, idesc);
    indexBuffer_.upload(device, queue, pool,
                        std::as_bytes(std::span{indices.data(), indices.size()}));

    valueMin_ = grid.valueRange.min;
    valueMax_ = grid.valueRange.max;
    gridXRange_ = grid.xRange;
    gridYRange_ = grid.yRange;
}

void SurfaceRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                           const plot::Camera3D& camera) const {
    if (!inited_ || indexCount_ == 0) return;

    struct PC {
        float vp[16];
        float gridXMin, gridXMax, gridYMin, gridYMax;
        float valueMin, valueMax;
    } pc;
    auto vp = camera.viewProjection();
    std::memcpy(pc.vp, vp.data(), sizeof(float) * 16);
    pc.gridXMin = gridXRange_.min;
    pc.gridXMax = gridXRange_.max;
    pc.gridYMin = gridYRange_.min;
    pc.gridYMax = gridYRange_.max;
    pc.valueMin = valueMin_;
    pc.valueMax = valueMax_;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.pushConstants(pipelineLayout_.get(),
                      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PC), &pc);

    vk::Viewport vp2;
    vp2.setX(static_cast<float>(rect.offset.x))
       .setY(static_cast<float>(rect.offset.y))
       .setWidth(static_cast<float>(rect.extent.width))
       .setHeight(static_cast<float>(rect.extent.height))
       .setMinDepth(0.0f).setMaxDepth(1.0f);
    cmd.setViewport(0, vp2);
    cmd.setScissor(0, rect);

    std::array<vk::Buffer, 1> buf = { vertexBuffer_.handle() };
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.bindIndexBuffer(indexBuffer_.handle(), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(indexCount_, 1, 0, 0, 0);
}

} // namespace volcano::render::primitives
