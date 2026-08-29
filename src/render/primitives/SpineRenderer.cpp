// volcano/render/primitives/SpineRenderer.cpp
#include "volcano/render/primitives/SpineRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <algorithm>
#include <cstring>

namespace volcano::render::primitives {

namespace {

struct LineVertex {
    float x, y;      // pixel position (top-left origin, Y-down)
    float r, g, b, a; // color
};

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(push_constant) uniform PC {
    vec2 u_resolution;
    float u_lineWidth;
} pc;
layout(location = 0) out vec4 v_color;
void main() {
    vec2 ndc = vec2(
        a_pos.x / pc.u_resolution.x * 2.0 - 1.0,
        a_pos.y / pc.u_resolution.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = v_color;
}
)";

} // namespace

void SpineRenderer::init(vk::Device device, VmaAllocator allocator,
                         vk::RenderPass renderPass,
                         vk::SampleCountFlagBits samples,
                         core::PipelineCache& /*cache*/,
                         core::DescriptorPool& /*descPool*/) {
    device_ = device;
    allocator_ = allocator;

    auto vertSpv = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, vertSpv);
    frag_ = core::ShaderModule(device, fragSpv);

    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 3);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription binding{};
    binding.setBinding(0).setStride(sizeof(LineVertex))
           .setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription attrs[2];
    attrs[0].setLocation(0).setBinding(0)
            .setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    attrs[1].setLocation(1).setBinding(0)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(LineVertex, r));

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(binding)
         .setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::eLineStrip);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples);

    // Depth testing disabled (spines are 2D overlays).
    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false)
        .setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att{};
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorBlendOp(vk::BlendOp::eAdd)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOne)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR
                        | vk::ColorComponentFlagBits::eG
                        | vk::ColorComponentFlagBits::eB
                        | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci{};
    cbsci.setAttachments(att);

    vk::DynamicState dynStates[] = { vk::DynamicState::eViewport,
                                     vk::DynamicState::eScissor,
                                     vk::DynamicState::eLineWidth };
    vk::PipelineDynamicStateCreateInfo dsci{};
    dsci.setDynamicStates(dynStates);

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
        .setRenderPass(renderPass);

    auto rv = device.createGraphicsPipelineUnique({}, gpci);
    pipeline_ = std::move(rv.value);

    // Create a second pipeline for filled rectangles (triangle list).
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);
    // Remove eLineWidth from dynamic states (not needed for triangles).
    vk::DynamicState fillDynStates[] = { vk::DynamicState::eViewport,
                                         vk::DynamicState::eScissor };
    dsci.setDynamicStates(fillDynStates);
    gpci.setPInputAssemblyState(&iaci)
        .setPDynamicState(&dsci);
    auto rv2 = device.createGraphicsPipelineUnique({}, gpci);
    fillPipeline_ = std::move(rv2.value);

    inited_ = true;
}

void SpineRenderer::ensureScratch(size_t byteCount) {
    if (scratchOffset_ + byteCount <= scratchCapacity_) return;
    size_t needed = scratchOffset_ + byteCount;
    size_t newSize = std::max<size_t>(16384, needed * 2);
    core::BufferDesc bdesc{};
    bdesc.size = newSize;
    bdesc.usage = core::BufferUsage::Vertex;
    bdesc.hostVisible = true;
    scratchVB_ = core::Buffer(allocator_, bdesc);
    scratchCapacity_ = newSize;
    scratchOffset_ = 0;
}

void SpineRenderer::drawLineStrip(vk::CommandBuffer cmd, vk::Rect2D scissor,
                                  std::span<const plot::Point2D> points,
                                  plot::Color color, float width) {
    if (!inited_ || points.empty()) return;

    size_t byteSize = points.size() * sizeof(LineVertex);
    ensureScratch(byteSize);

    auto* verts = reinterpret_cast<LineVertex*>(
        static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_);
    for (size_t i = 0; i < points.size(); ++i) {
        verts[i].x = points[i].x;
        verts[i].y = points[i].y;
        verts[i].r = color.r; verts[i].g = color.g;
        verts[i].b = color.b; verts[i].a = color.a;
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());

    struct PC { float w, h, lw; } pc{
        float(scissor.extent.width), float(scissor.extent.height), width};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(scissor.extent.width),
                          float(scissor.extent.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, scissor);
    cmd.setLineWidth(width);

    cmd.draw(static_cast<uint32_t>(points.size()), 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRenderer::drawRect(vk::CommandBuffer cmd, vk::Rect2D scissor,
                             plot::Rect2D rect, plot::Color color,
                             float lineWidth) {
    if (!inited_) return;
    // 5 points for a closed rectangle (line strip).
    plot::Point2D pts[5] = {
        {float(rect.x), float(rect.y)},
        {float(rect.x + rect.width), float(rect.y)},
        {float(rect.x + rect.width), float(rect.y + rect.height)},
        {float(rect.x), float(rect.y + rect.height)},
        {float(rect.x), float(rect.y)},
    };
    drawLineStrip(cmd, scissor, pts, color, lineWidth);
}

void SpineRenderer::drawFilledRect(vk::CommandBuffer cmd, vk::Rect2D scissor,
                                   plot::Rect2D rect, plot::Color color) {
    if (!inited_) return;
    // 6 vertices for two triangles forming a rectangle.
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = float(rect.x + rect.width), y1 = float(rect.y + rect.height);
    LineVertex verts[6] = {
        {x0, y0, color.r, color.g, color.b, color.a},
        {x1, y0, color.r, color.g, color.b, color.a},
        {x1, y1, color.r, color.g, color.b, color.a},
        {x0, y0, color.r, color.g, color.b, color.a},
        {x1, y1, color.r, color.g, color.b, color.a},
        {x0, y1, color.r, color.g, color.b, color.a},
    };

    size_t byteSize = 6 * sizeof(LineVertex);
    ensureScratch(byteSize);
    void* dst = static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_;
    std::memcpy(dst, verts, byteSize);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, fillPipeline_.get());

    struct PC { float w, h, lw; } pc{
        float(scissor.extent.width), float(scissor.extent.height), 1.0f};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(scissor.extent.width),
                          float(scissor.extent.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, scissor);

    cmd.draw(6, 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRenderer::drawTicks(vk::CommandBuffer cmd, vk::Rect2D scissor,
                              plot::Rect2D rect, std::span<const float> positions,
                              plot::Color color, float tickLength,
                              bool yAxis, float dataMin, float dataMax) {
    if (!inited_ || positions.empty()) return;

    float range = dataMax - dataMin;
    if (range <= 0) return;

    std::vector<plot::Point2D> points;
    points.reserve(positions.size() * 2);

    if (!yAxis) {
        // X-axis ticks: below the axes, pointing down.
        for (float pos : positions) {
            float px = rect.x + (pos - dataMin) / range * rect.width;
            if (px < rect.x || px > rect.x + rect.width) continue;
            points.push_back({px, float(rect.y + rect.height)});
            points.push_back({px, float(rect.y + rect.height + tickLength)});
        }
    } else {
        // Y-axis ticks: left of the axes, pointing left.
        for (float pos : positions) {
            float py = rect.y + rect.height - (pos - dataMin) / range * rect.height;
            if (py < rect.y || py > rect.y + rect.height) continue;
            points.push_back({float(rect.x), py});
            points.push_back({float(rect.x - tickLength), py});
        }
    }

    if (points.empty()) return;

    // Draw as separate line segments (pairs of points).
    // We use line strip topology, but need to break between segments.
    // Simplest: draw each pair as a separate draw call.
    // Actually, for line strip, consecutive points are connected.
    // To draw separate segments, we can use line list topology instead.
    // But our pipeline is set up for line strip. Let's just draw each pair.
    // For efficiency, we could use LINE_LIST topology. Let's change the
    // pipeline to support both, or just draw each segment individually.
    //
    // For now, draw each tick as a 2-point line strip.
    for (size_t i = 0; i < points.size(); i += 2) {
        std::span<const plot::Point2D> seg(&points[i], 2);
        drawLineStrip(cmd, scissor, seg, color, 1.0f);
    }
}

} // namespace volcano::render::primitives
