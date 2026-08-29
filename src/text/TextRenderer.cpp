// volcano/text/TextRenderer.cpp — glyb-based bitmap atlas text renderer
#include "volcano/text/TextRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// glyb headers (must be included in dependency order — glyb headers
// don't include their own dependencies)
#include "binpack.h"
#include "utf8.h"
#include "image.h"
#include "draw.h"
#include "font.h"
#include "glyph.h"

namespace volcano::text {

namespace {

// glyb draw_vertex layout: pos[3], uv[2], color(u32), shape(f32) = 28 bytes
// But we use a simplified vertex for our Vulkan pipeline.
struct TextVertex {
    float x, y;        // pixel position (screen-space, Y-down)
    float u, v;        // atlas UV
    float r, g, b, a;  // color
};

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(push_constant) uniform PC {
    vec2 u_resolution;
} pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() {
    // Pixel coords (top-left origin, Y-down) → Vulkan NDC (Y-down).
    vec2 ndc = vec2(
        a_pos.x / pc.u_resolution.x * 2.0 - 1.0,
        a_pos.y / pc.u_resolution.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D u_atlas;
void main() {
    float alpha = texture(u_atlas, v_uv).r;
    outColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

// Find a system font file (DejaVu Sans or similar).
std::string findSystemFontFile() {
    std::vector<std::filesystem::path> dirs = {
        "/usr/share/fonts", "/usr/local/share/fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".local/share/fonts",
    };
    auto isRegular = [](const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.find("bold") == std::string::npos &&
               lower.find("italic") == std::string::npos &&
               lower.find("oblique") == std::string::npos &&
               lower.find("condensed") == std::string::npos;
    };
    // First pass: look for DejaVu Sans regular.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            if (name.find("DejaVuSans") != std::string::npos && isRegular(name))
                return e.path().string();
        }
    }
    // Second pass: any regular TTF.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            auto ext = e.path().extension().string();
            std::string extLower = ext;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
            if ((extLower == ".ttf" || extLower == ".otf") && isRegular(name))
                return e.path().string();
        }
    }
    return {};
}

} // namespace

TextRenderer::TextRenderer() = default;
TextRenderer::~TextRenderer() {
    delete static_cast<draw_list*>(batch_);
}

void TextRenderer::init(vk::Device device, VmaAllocator allocator,
                        vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples,
                        core::PipelineCache& /*cache*/,
                        core::DescriptorPool& /*descPool*/) {
    device_ = device;
    allocator_ = allocator;

    // --- glyb font manager, shaper, renderer ---
    fontManager_ = std::make_unique<font_manager_ft>();
    shaper_ = std::make_unique<text_shaper_hb>();
    textRenderer_ = std::make_unique<text_renderer_ft>(fontManager_.get());
    batch_ = new draw_list();

    loadFont();

    // --- Vulkan pipeline for textured quads ---

    // Descriptor set layout: one combined image sampler (binding 0).
    vk::DescriptorSetLayoutBinding binding{};
    binding.setBinding(0)
           .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
           .setDescriptorCount(1)
           .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    vk::DescriptorSetLayoutCreateInfo dslci{};
    dslci.setBindings(binding);
    descSetLayout_ = device.createDescriptorSetLayoutUnique(dslci);

    // Descriptor pool for the atlas texture.
    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1);
    vk::DescriptorPoolCreateInfo dpci{};
    dpci.setPoolSizes(poolSize)
        .setMaxSets(1)
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    descPool_ = device.createDescriptorPoolUnique(dpci);

    // Allocate descriptor set.
    vk::DescriptorSetLayout layouts[] = { descSetLayout_.get() };
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.setDescriptorPool(descPool_.get())
        .setSetLayouts(layouts);
    auto sets = device.allocateDescriptorSets(dsai);
    descSet_ = sets[0];

    // Sampler.
    vk::SamplerCreateInfo sci{};
    sci.setMagFilter(vk::Filter::eLinear)
       .setMinFilter(vk::Filter::eLinear)
       .setMipmapMode(vk::SamplerMipmapMode::eLinear)
       .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
       .setBorderColor(vk::BorderColor::eFloatTransparentBlack)
       .setUnnormalizedCoordinates(VK_FALSE);
    sampler_ = device.createSamplerUnique(sci);

    // Pipeline layout.
    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 2);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descSetLayout_.get())
        .setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    // Shaders.
    auto vertSpv = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    core::ShaderModule vertMod(device, vertSpv);
    core::ShaderModule fragMod(device, fragSpv);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vertMod.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(fragMod.handle()).setPName("main");

    // Vertex input: pos(2f), uv(2f), color(4f) = 32 bytes
    vk::VertexInputBindingDescription vbind{};
    vbind.setBinding(0).setStride(sizeof(TextVertex))
         .setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription vattrs[3];
    vattrs[0].setLocation(0).setBinding(0)
             .setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    vattrs[1].setLocation(1).setBinding(0)
             .setFormat(vk::Format::eR32G32Sfloat)
             .setOffset(offsetof(TextVertex, u));
    vattrs[2].setLocation(2).setBinding(0)
             .setFormat(vk::Format::eR32G32B32A32Sfloat)
             .setOffset(offsetof(TextVertex, r));

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(vbind)
         .setVertexAttributeDescriptions(vattrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

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

void TextRenderer::loadFont() {
    auto fontPath = findSystemFontFile();
    if (fontPath.empty()) return;
    fontManager_->scanFontPath(fontPath);
    fontFace_ = fontManager_->findFontByPath(fontPath);
}

void TextRenderer::resetScratch() {
    // Nothing to reset — we rebuild the draw list each frame.
}

void TextRenderer::ensureScratch(size_t vertexBytes, size_t indexBytes) {
    if (vertexBytes > vbCapacity_) {
        size_t newSize = std::max<size_t>(65536, vertexBytes * 2);
        core::BufferDesc bdesc{};
        bdesc.size = newSize;
        bdesc.usage = core::BufferUsage::Vertex;
        bdesc.hostVisible = true;
        scratchVB_ = core::Buffer(allocator_, bdesc);
        vbCapacity_ = newSize;
    }
    if (indexBytes > ibCapacity_) {
        size_t newSize = std::max<size_t>(65536, indexBytes * 2);
        core::BufferDesc bdesc{};
        bdesc.size = newSize;
        bdesc.usage = core::BufferUsage::Index;
        bdesc.hostVisible = true;
        scratchIB_ = core::Buffer(allocator_, bdesc);
        ibCapacity_ = newSize;
    }
}

void TextRenderer::prepareAtlas(vk::Queue queue, vk::CommandPool pool) {
    if (atlasUploaded_ || !fontFace_) return;

    // Pre-render common ASCII characters to populate the atlas.
    // This ensures all common glyphs are rasterized before upload.
    auto* batch = static_cast<draw_list*>(batch_);
    std::string charset;
    for (char c = 32; c < 127; ++c) charset.push_back(c);

    int font_size = int(16.0f * 64.0f);  // 16px in 26.6 fixed-point
    std::string lang = "en";
    text_segment segment(charset, lang, fontFace_, font_size, 0, 0, 0xff000000);

    std::vector<glyph_shape> shapes;
    draw_list_clear(*batch);
    shaper_->shape(shapes, segment);
    textRenderer_->render(*batch, shapes, segment);

    // Get the atlas (now populated with glyphs).
    auto* atlas = fontManager_->getCurrentAtlas(fontFace_);
    if (!atlas || !atlas->pixels) return;

    atlasWidth_ = (int)atlas->width;
    atlasHeight_ = (int)atlas->height;

    // Create the Vulkan image (R8_UNORM for grayscale atlas).
    core::ImageDesc idesc{};
    idesc.format = vk::Format::eR8Unorm;
    idesc.extent = vk::Extent2D{uint32_t(atlasWidth_), uint32_t(atlasHeight_)};
    idesc.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    idesc.tiling = vk::ImageTiling::eOptimal;
    atlasImage_ = core::Image(allocator_, idesc);

    // Create image view.
    vk::ImageViewCreateInfo ivci{};
    ivci.setImage(atlasImage_.handle())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8Unorm)
        .setSubresourceRange(vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    atlasView_ = device_.createImageViewUnique(ivci);

    // Create a staging buffer and copy atlas pixels.
    size_t pixelBytes = size_t(atlasWidth_) * size_t(atlasHeight_);
    core::BufferDesc sdesc{};
    sdesc.size = pixelBytes;
    sdesc.usage = core::BufferUsage::Staging;
    sdesc.hostVisible = true;
    core::Buffer staging(allocator_, sdesc);
    std::memcpy(staging.mappedData(), atlas->pixels, pixelBytes);

    // Use a one-time command buffer (OUTSIDE any render pass).
    vk::CommandBufferAllocateInfo cbai{};
    cbai.setCommandPool(pool)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);
    auto cmds = device_.allocateCommandBuffers(cbai);
    vk::CommandBuffer cmd = cmds[0];

    vk::CommandBufferBeginInfo cbbi{};
    cbbi.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd.begin(cbbi);

    // Transition image to transfer dst.
    core::Image::transitionLayout(cmd, atlasImage_.handle(),
        vk::Format::eR8Unorm,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal);

    // Copy buffer to image.
    vk::BufferImageCopy region{};
    region.setBufferOffset(0)
          .setBufferRowLength(atlasWidth_)
          .setBufferImageHeight(atlasHeight_)
          .setImageSubresource(vk::ImageSubresourceLayers{
              vk::ImageAspectFlagBits::eColor, 0, 0, 1})
          .setImageOffset(vk::Offset3D{0, 0, 0})
          .setImageExtent(vk::Extent3D{uint32_t(atlasWidth_), uint32_t(atlasHeight_), 1});
    cmd.copyBufferToImage(staging.handle(), atlasImage_.handle(),
                          vk::ImageLayout::eTransferDstOptimal, region);

    // Transition image to shader read.
    core::Image::transitionLayout(cmd, atlasImage_.handle(),
        vk::Format::eR8Unorm,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal);

    cmd.end();

    // Submit and wait.
    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(cmd);
    queue.submit(submitInfo);
    queue.waitIdle();

    // Free the command buffer.
    device_.freeCommandBuffers(pool, cmd);

    // Update descriptor set.
    vk::DescriptorImageInfo dii{};
    dii.setSampler(sampler_.get())
       .setImageView(atlasView_.get())
       .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet wds{};
    wds.setDstSet(descSet_)
       .setDstBinding(0)
       .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
       .setImageInfo(dii);
    device_.updateDescriptorSets(wds, {});

    atlasUploaded_ = true;
}

void TextRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        std::string_view text, float x, float y,
                        plot::Color color, float scale) {
    if (!inited_ || !fontFace_ || text.empty()) return;

    // Font size in 26.6 fixed-point (glyb convention).
    int font_size = int(16.0f * scale * 64.0f);

    // Shape text using glyb.
    std::vector<glyph_shape> shapes;
    std::string lang = "en";
    uint32_t rgba = (uint32_t(color.r * 255) << 24) |
                    (uint32_t(color.g * 255) << 16) |
                    (uint32_t(color.b * 255) << 8)  |
                    (uint32_t(color.a * 255));
    text_segment segment(std::string(text), lang, fontFace_,
                         font_size, x, y, rgba);

    auto* batch = static_cast<draw_list*>(batch_);

    draw_list_clear(*batch);
    shaper_->shape(shapes, segment);
    textRenderer_->render(*batch, shapes, segment);

    if (batch->vertices.empty() || batch->indices.empty()) return;

    // Atlas must have been uploaded via prepareAtlas() before any draw calls.
    if (!atlasUploaded_) return;

    // Convert glyb draw_vertex to our TextVertex.
    // glyb vertex: pos[3], uv[2], color(u32), shape(f32)
    // Our vertex: x, y, u, v, r, g, b, a
    size_t vertCount = batch->vertices.size();
    size_t idxCount = batch->indices.size();
    size_t vertBytes = vertCount * sizeof(TextVertex);
    size_t idxBytes = idxCount * sizeof(uint32_t);
    ensureScratch(vertBytes, idxBytes);

    // Convert vertices.
    auto* dstVerts = static_cast<TextVertex*>(scratchVB_.mappedData());
    for (size_t i = 0; i < vertCount; ++i) {
        const auto& sv = batch->vertices[i];
        dstVerts[i].x = sv.pos[0];
        dstVerts[i].y = sv.pos[1];
        dstVerts[i].u = sv.uv[0];
        dstVerts[i].v = sv.uv[1];
        // glyb color is RGBA8 packed as uint32.
        uint32_t c = sv.color;
        dstVerts[i].r = ((c >> 24) & 0xff) / 255.0f;
        dstVerts[i].g = ((c >> 16) & 0xff) / 255.0f;
        dstVerts[i].b = ((c >> 8) & 0xff) / 255.0f;
        dstVerts[i].a = (c & 0xff) / 255.0f;
    }

    // Copy indices.
    auto* dstIdx = static_cast<uint32_t*>(scratchIB_.mappedData());
    std::memcpy(dstIdx, batch->indices.data(), idxBytes);

    // Bind pipeline and descriptor set.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           pipelineLayout_.get(), 0, descSet_, {});

    // Push constant: framebuffer resolution.
    struct PC { float w, h; } pc{float(rect.extent.width), float(rect.extent.height)};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    // Bind vertex and index buffers.
    vk::DeviceSize vbOffset = 0;
    cmd.bindVertexBuffers(0, scratchVB_.handle(), vbOffset);
    cmd.bindIndexBuffer(scratchIB_.handle(), 0, vk::IndexType::eUint32);

    // Set viewport + scissor.
    vk::Viewport viewport{0, 0, float(rect.extent.width), float(rect.extent.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, rect);

    // Draw all indices.
    cmd.drawIndexed(uint32_t(idxCount), 1, 0, 0, 0);
}

} // namespace volcano::text
