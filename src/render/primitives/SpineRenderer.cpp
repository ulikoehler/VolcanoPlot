// volcano/render/primitives/SpineRenderer.cpp — Vulkan impl (SpineRendererVk)
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/plot/Path.hpp"
#include "../VkFactory.hpp"
#include "../VulkanGpuServices.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <vector>

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

/// Convert the backend-neutral scissor to a vk::Rect2D (same top-left
/// convention — no Y-flip in scissor space).
[[nodiscard]] inline vk::Rect2D vkScissor(plot::Rect2D r) noexcept {
    return {vk::Offset2D{r.x, r.y}, vk::Extent2D{r.width, r.height}};
}

class SpineRendererVk final : public SpineRenderer {
public:
    explicit SpineRendererVk(VulkanGpuServices& svcs) : svcs_(&svcs) {}

    void init();

    void drawRect(Cmd& cmd, plot::Rect2D clip,
                  plot::Extent2D resolution,
                  plot::Rect2D rect, plot::Color color,
                  float lineWidth) override;
    void drawFilledRect(Cmd& cmd, plot::Rect2D clip,
                        plot::Extent2D resolution,
                        plot::Rect2D rect, plot::Color color) override;
    void resetScratch() override {
        scratchOffset_ = 0;
        retiredScratch_.clear();
    }
    void drawTicks(Cmd& cmd, plot::Rect2D clip,
                   plot::Extent2D resolution,
                   plot::Rect2D rect,
                   std::span<const float> positions,
                   plot::Color color, float tickLength,
                   bool yAxis, float dataMin, float dataMax,
                   float inFrac = 0.0f, bool farSide = false,
                   float tickWidth = 2.0f) override;
    void drawLineStrip(Cmd& cmd, plot::Rect2D clip,
                       plot::Extent2D resolution,
                       std::span<const plot::Point2D> points,
                       plot::Color color, float width) override;
    void drawTriangles(Cmd& cmd, plot::Rect2D clip,
                       plot::Extent2D resolution,
                       std::span<const plot::Point2D> triVerts,
                       plot::Color color) override;
    void drawTrianglesVC(Cmd& cmd, plot::Rect2D clip,
                         plot::Extent2D resolution,
                         std::span<const plot::Point2D> triVerts,
                         std::span<const plot::Color> colors) override;
    void drawTrianglesGpu(Cmd& cmd, plot::Rect2D clip,
                          plot::Extent2D resolution,
                          GpuBuf buffer, uint64_t byteOffset,
                          uint32_t vertexCount) override;

private:
    VulkanGpuServices* svcs_;
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;        // line strip pipeline
    vk::UniquePipeline fillPipeline_;    // triangle list pipeline (filled rects)
    bool inited_ = false;

    /// Scratch vertex buffer (host-visible, ring-buffered).
    core::Buffer scratchVB_;
    /// Scratch buffers retired by ensureScratch growth this frame; kept
    /// alive because recorded draw commands still reference them.
    std::vector<core::Buffer> retiredScratch_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;

    void ensureScratch(size_t byteCount);
};

void SpineRendererVk::init() {
    device_ = svcs_->device();
    allocator_ = svcs_->allocator();
    const auto renderPass = svcs_->renderPass();
    const auto samples = svcs_->samples();
    const vk::Device device = device_;

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
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
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

void SpineRendererVk::ensureScratch(size_t byteCount) {
    if (scratchOffset_ + byteCount <= scratchCapacity_) return;
    size_t needed = scratchOffset_ + byteCount;
    size_t newSize = std::max<size_t>(1u << 20, needed * 2);
    // The old buffer is still referenced by draw commands recorded this
    // frame — keep it alive until resetScratch() at the next frame.
    if (scratchVB_.handle() != VK_NULL_HANDLE)
        retiredScratch_.push_back(std::move(scratchVB_));
    core::BufferDesc bdesc{};
    bdesc.size = newSize;
    bdesc.usage = core::BufferUsage::Vertex;
    bdesc.hostVisible = true;
    scratchVB_ = core::Buffer(allocator_, bdesc);
    scratchCapacity_ = newSize;
    scratchOffset_ = 0;
}

void SpineRendererVk::drawLineStrip(Cmd& cmdRef, plot::Rect2D clip,
                                  plot::Extent2D resolution,
                                  std::span<const plot::Point2D> points,
                                  plot::Color color, float width) {
    const auto cmd = vkCmd(cmdRef);
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
        float(resolution.width), float(resolution.height), width};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vkScissor(clip));
    cmd.setLineWidth(width);

    cmd.draw(static_cast<uint32_t>(points.size()), 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRendererVk::drawRect(Cmd& cmd, plot::Rect2D clip,
                             plot::Extent2D resolution,
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
    drawLineStrip(cmd, clip, resolution, pts, color, lineWidth);
}

void SpineRendererVk::drawFilledRect(Cmd& cmdRef, plot::Rect2D clip,
                                   plot::Extent2D resolution,
                                   plot::Rect2D rect, plot::Color color) {
    const auto cmd = vkCmd(cmdRef);
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
        float(resolution.width), float(resolution.height), 1.0f};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vkScissor(clip));

    cmd.draw(6, 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRendererVk::drawTriangles(Cmd& cmdRef, plot::Rect2D clip,
                                  plot::Extent2D resolution,
                                  std::span<const plot::Point2D> triVerts,
                                  plot::Color color) {
    const auto cmd = vkCmd(cmdRef);
    if (!inited_ || triVerts.empty()) return;

    size_t count = triVerts.size();
    size_t byteSize = count * sizeof(LineVertex);
    ensureScratch(byteSize);

    auto* verts = reinterpret_cast<LineVertex*>(
        static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_);
    for (size_t i = 0; i < count; ++i) {
        verts[i].x = triVerts[i].x;
        verts[i].y = triVerts[i].y;
        verts[i].r = color.r; verts[i].g = color.g;
        verts[i].b = color.b; verts[i].a = color.a;
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, fillPipeline_.get());

    struct PC { float w, h, lw; } pc{
        float(resolution.width), float(resolution.height), 1.0f};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vkScissor(clip));

    cmd.draw(static_cast<uint32_t>(count), 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRendererVk::drawTrianglesVC(
    Cmd& cmdRef, plot::Rect2D clip, plot::Extent2D resolution,
    std::span<const plot::Point2D> triVerts,
    std::span<const plot::Color> colors) {
    const auto cmd = vkCmd(cmdRef);
    if (!inited_ || triVerts.empty()) return;
    size_t count = triVerts.size();
    if (colors.size() < count) return;

    size_t byteSize = count * sizeof(LineVertex);
    ensureScratch(byteSize);

    auto* verts = reinterpret_cast<LineVertex*>(
        static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_);
    for (size_t i = 0; i < count; ++i) {
        verts[i].x = triVerts[i].x;
        verts[i].y = triVerts[i].y;
        verts[i].r = colors[i].r; verts[i].g = colors[i].g;
        verts[i].b = colors[i].b; verts[i].a = colors[i].a;
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, fillPipeline_.get());

    struct PC { float w, h, lw; } pc{
        float(resolution.width), float(resolution.height), 1.0f};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vkScissor(clip));

    cmd.draw(static_cast<uint32_t>(count), 1, 0, 0);

    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

void SpineRendererVk::drawTrianglesGpu(Cmd& cmdRef, plot::Rect2D clip,
                                     plot::Extent2D resolution,
                                     GpuBuf buffer,
                                     uint64_t byteOffset,
                                     uint32_t vertexCount) {
    const auto cmd = vkCmd(cmdRef);
    const auto buf = svcs_->vkBufferOf(buffer);
    if (!inited_ || vertexCount == 0 || !buf) return;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, fillPipeline_.get());

    struct PC { float w, h, lw; } pc{
        float(resolution.width), float(resolution.height), 1.0f};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    cmd.bindVertexBuffers(0, buf, {vk::DeviceSize(byteOffset)});

    vk::Viewport viewport{0, 0, float(resolution.width),
                          float(resolution.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vkScissor(clip));

    cmd.draw(vertexCount, 1, 0, 0);
}

void SpineRendererVk::drawTicks(Cmd& cmd, plot::Rect2D clip,
                              plot::Extent2D resolution,
                              plot::Rect2D rect, std::span<const float> positions,
                              plot::Color color, float tickLength,
                              bool yAxis, float dataMin, float dataMax,
                              float inFrac, bool farSide, float tickWidth) {
    if (!inited_ || positions.empty() || tickLength <= 0.0f) return;

    float range = dataMax - dataMin;
    if (range <= 0) return;

    std::vector<plot::Point2D> points;
    points.reserve(positions.size() * 2);

    // Tick segment: inner end at edge - d*len*inFrac (into the axes),
    // outer end at edge + d*len*(1-inFrac) (outward). d = outward sign.
    if (!yAxis) {
        // X-axis ticks at the bottom edge (or top edge when farSide).
        float edge = farSide ? rect.y : rect.y + rect.height;
        float d = farSide ? -1.0f : 1.0f; // outward direction
        for (float pos : positions) {
            float px = rect.x + (pos - dataMin) / range * rect.width;
            if (px < rect.x || px > rect.x + rect.width) continue;
            points.push_back({px, edge - d * tickLength * inFrac});
            points.push_back({px, edge + d * tickLength * (1.0f - inFrac)});
        }
    } else {
        // Y-axis ticks at the left edge (or right edge when farSide).
        float edge = farSide ? rect.x + rect.width : rect.x;
        float d = farSide ? 1.0f : -1.0f;
        for (float pos : positions) {
            float py = rect.y + rect.height - (pos - dataMin) / range * rect.height;
            if (py < rect.y || py > rect.y + rect.height) continue;
            points.push_back({edge - d * tickLength * inFrac, py});
            points.push_back({edge + d * tickLength * (1.0f - inFrac), py});
        }
    }

    if (points.empty()) return;

    for (size_t i = 0; i < points.size(); i += 2) {
        std::span<const plot::Point2D> seg(&points[i], 2);
        drawLineStrip(cmd, clip, resolution, seg, color, tickWidth);
    }
}

std::unique_ptr<SpineRenderer> makeSpineVk(VulkanGpuServices& svcs) {
    auto p = std::make_unique<SpineRendererVk>(svcs);
    p->init();
    return p;
}

} // namespace volcano::render::primitives
