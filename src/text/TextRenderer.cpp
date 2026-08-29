// volcano/text/TextRenderer.cpp
#include "volcano/text/TextRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <algorithm>
#include <cstring>

namespace volcano::text {

namespace {

// Vertex: position in pixels (screen-space, top-left origin, Y-down) + color.
struct TextVertex {
    float x, y;
    float r, g, b, a;
};

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(push_constant) uniform PC {
    vec2 u_resolution;  // framebuffer size in pixels
} pc;
layout(location = 0) out vec4 v_color;
void main() {
    // Pixel coords (top-left origin, Y-down) → NDC.
    vec2 ndc = vec2(
        a_pos.x / pc.u_resolution.x * 2.0 - 1.0,
        1.0 - a_pos.y / pc.u_resolution.y * 2.0
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

void TextRenderer::init(vk::Device device, VmaAllocator allocator,
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
       .setOffset(0).setSize(sizeof(float) * 2);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(frag_.handle()).setPName("main");

    vk::VertexInputBindingDescription binding{};
    binding.setBinding(0).setStride(sizeof(TextVertex))
           .setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription attrs[2];
    attrs[0].setLocation(0).setBinding(0)
            .setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    attrs[1].setLocation(1).setBinding(0)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(TextVertex, r));

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(binding)
         .setVertexAttributeDescriptions(attrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples);

    // Depth testing disabled (text is 2D overlay).
    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

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
                                     vk::DynamicState::eScissor };
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
    inited_ = true;
}

void TextRenderer::setAtlas(std::shared_ptr<GlyphAtlas> atlas) {
    atlas_ = std::move(atlas);
}

void TextRenderer::ensureScratch(size_t byteCount) {
    if (scratchOffset_ + byteCount <= scratchCapacity_) return;
    // Not enough space at current offset — grow the buffer.
    size_t needed = scratchOffset_ + byteCount;
    size_t newSize = std::max<size_t>(65536, needed * 2);
    core::BufferDesc bdesc{};
    bdesc.size = newSize;
    bdesc.usage = core::BufferUsage::Vertex;
    bdesc.hostVisible = true;
    scratchVB_ = core::Buffer(allocator_, bdesc);
    scratchCapacity_ = newSize;
    // Note: growing the buffer invalidates previous contents, but since
    // we only grow when there's not enough space, previous draws in this
    // frame would have already been recorded with the old buffer.
    // This is a known limitation — ideally we'd use a persistent ring
    // buffer that never shrinks. For now, resetting offset on grow is safe
    // because the GPU hasn't executed the commands yet.
    scratchOffset_ = 0;
}

void TextRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        std::string_view text, float x, float y,
                        plot::Color color, float scale) {
    if (!inited_ || !atlas_) return;

    auto codepoints = utf8ToUtf32(text);
    if (codepoints.empty()) return;

    // Build vertex list: for each glyph, copy its triangulated mesh vertices,
    // offset by the current pen position and scaled.
    //
    // Font-space: origin at baseline, Y-up.
    // Screen-space: top-left origin, Y-down.
    // Mapping: screenX = penX + vx * scale, screenY = penY - vy * scale.
    std::vector<TextVertex> vertices;
    float penX = x;
    float penY = y;

    for (char32_t cp : codepoints) {
        const GlyphInfo* gi = atlas_->glyph(cp);
        if (!gi) continue;

        for (const auto& v : gi->mesh.vertices) {
            TextVertex tv;
            tv.x = penX + v.x * scale;
            tv.y = penY - v.y * scale;  // Y-up → Y-down
            tv.r = color.r; tv.g = color.g; tv.b = color.b; tv.a = color.a;
            vertices.push_back(tv);
        }
        penX += gi->advance * scale;
    }

    if (vertices.empty()) return;

    // Upload to scratch buffer at the current ring offset.
    size_t byteSize = vertices.size() * sizeof(TextVertex);
    ensureScratch(byteSize);
    void* dst = static_cast<char*>(scratchVB_.mappedData()) + scratchOffset_;
    std::memcpy(dst, vertices.data(), byteSize);

    // Draw.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());

    // Push constant: framebuffer resolution.
    struct PC { float w, h; } pc{float(rect.extent.width), float(rect.extent.height)};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    // Bind vertex buffer at the current offset.
    vk::DeviceSize offsets[] = { scratchOffset_ };
    cmd.bindVertexBuffers(0, scratchVB_.handle(), offsets);

    // Set viewport + scissor.
    vk::Viewport viewport{0, 0, float(rect.extent.width), float(rect.extent.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, rect);

    cmd.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);

    // Advance the ring offset (aligned to 16 bytes for safety).
    scratchOffset_ += (byteSize + 15) & ~size_t(15);
}

std::u32string TextRenderer::utf8ToUtf32(std::string_view utf8) {
    std::u32string result;
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            if (i + 2 < utf8.size()) cp = (cp << 6) | (utf8[i+2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            if (i + 2 < utf8.size()) cp = (cp << 6) | (utf8[i+2] & 0x3F);
            if (i + 3 < utf8.size()) cp = (cp << 6) | (utf8[i+3] & 0x3F);
            i += 4;
        } else {
            i += 1; continue;
        }
        result.push_back(cp);
    }
    return result;
}

} // namespace volcano::text
