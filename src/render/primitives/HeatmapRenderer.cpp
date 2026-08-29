// volcano/render/primitives/HeatmapRenderer.cpp
#include "volcano/render/primitives/HeatmapRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/CommandBuffer.hpp>
#include <volcano/plot/Transform.hpp>
#include <array>
#include <stdexcept>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;  // fullscreen quad [-1,1]

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;  // xy = min, zw = span (data coords)
    vec4 u_gridRange;    // xy = xRange, zw = yRange (data coords of grid)
    vec2 u_valueRange;   // min, max of scalar values
} pc;

layout(location = 0) out vec2 v_uv;  // texture coords [0,1]

void main() {
    // u_gridRange = (xMin, xMax, yMin, yMax)
    // Map NDC quad position to data coords within the grid range.
    vec2 data = vec2(pc.u_gridRange.x, pc.u_gridRange.z) +
                (a_pos * 0.5 + 0.5) * vec2(pc.u_gridRange.y - pc.u_gridRange.x,
                                           pc.u_gridRange.w - pc.u_gridRange.z);
    // Map data coords to NDC for the viewport.
    vec2 ndc = (data - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    // UV into grid texture: flip Y for Vulkan texture origin.
    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(set = 0, binding = 0) uniform sampler2D u_grid;
layout(set = 0, binding = 1) uniform sampler2D u_cmap;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;
    vec4 u_gridRange;
    vec2 u_valueRange;
} pc;

void main() {
    float v = texture(u_grid, v_uv).r;
    float t = (v - pc.u_valueRange.x) / max(pc.u_valueRange.y - pc.u_valueRange.x, 1e-30);
    t = clamp(t, 0.0, 1.0);
    outColor = texture(u_cmap, vec2(t, 0.5));
}
)";

constexpr float kQuad[] = {
    -1,-1,  1,-1,  -1, 1,
    -1, 1,  1,-1,   1, 1,
};

} // namespace

void HeatmapRenderer::init(vk::Device device, vk::RenderPass renderPass,
                           vk::SampleCountFlagBits samples, core::PipelineCache& cache,
                           core::DescriptorPool& descPool) {
    device_ = device;
    auto v = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto f = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    vert_ = core::ShaderModule(device, v);
    frag_ = core::ShaderModule(device, f);

    // Descriptor set layout: 2 combined image samplers.
    vk::DescriptorSetLayoutBinding bindings[2];
    bindings[0].setBinding(0).setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
               .setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eFragment);
    bindings[1].setBinding(1).setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
               .setDescriptorCount(1).setStageFlags(vk::ShaderStageFlagBits::eFragment);
    vk::DescriptorSetLayoutCreateInfo dslci;
    dslci.setBindings(bindings);
    descLayout_ = device.createDescriptorSetLayoutUnique(dslci);

    vk::PushConstantRange pc;
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
       .setOffset(0).setSize(sizeof(float) * 10);
    vk::PipelineLayoutCreateInfo plci;
    plci.setSetLayouts(descLayout_.get()).setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex).setModule(vert_.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment).setModule(frag_.handle()).setPName("main");

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
    if (res.result != vk::Result::eSuccess) throw std::runtime_error("Heatmap pipeline failed");
    pipeline_ = std::move(res.value);

    // Sampler
    vk::SamplerCreateInfo sci;
    sci.setMagFilter(vk::Filter::eLinear).setMinFilter(vk::Filter::eLinear)
       .setMipmapMode(vk::SamplerMipmapMode::eLinear)
       .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
    sampler_ = device.createSamplerUnique(sci);

    // Allocate descriptor set
    descSet_ = descPool.allocate(descLayout_.get());

    // Upload fullscreen quad
    // (Stored in a static buffer — created at upload time with allocator.)
    inited_ = true;
}

void HeatmapRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                             VmaAllocator allocator, const plot::Grid2D& grid,
                             const plot::Colormap& cmap) {
    // Upload grid as R32_SFLOAT image.
    {
        core::ImageDesc idesc{};
        idesc.format = vk::Format::eR32Sfloat;
        idesc.extent = vk::Extent2D{grid.width, grid.height};
        idesc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        idesc.tiling = vk::ImageTiling::eOptimal;
        gridImage_ = core::Image(allocator, idesc);

        // Staging buffer + copy
        core::BufferDesc bdesc{};
        bdesc.size = grid.values.size() * sizeof(float);
        bdesc.usage = core::BufferUsage::Staging;
        bdesc.hostVisible = true;
        core::Buffer staging(allocator, bdesc);
        std::memcpy(staging.mappedData(), grid.values.data(), bdesc.size);

        core::OneTimeCommands cmd(device, pool, queue);
        vk::BufferImageCopy region{};
        region.setBufferOffset(0)
              .setBufferRowLength(grid.width)
              .setBufferImageHeight(grid.height)
              .setImageSubresource(vk::ImageSubresourceLayers{}
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
              .setImageOffset({0,0,0})
              .setImageExtent({grid.width, grid.height, 1});
        // Transition to transfer dst
        core::Image::transitionLayout(cmd.handle(), gridImage_.handle(),
            vk::Format::eR32Sfloat, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal);
        cmd.handle().copyBufferToImage(staging.handle(), gridImage_.handle(),
            vk::ImageLayout::eTransferDstOptimal, region);
        // Transition to shader read
        core::Image::transitionLayout(cmd.handle(), gridImage_.handle(),
            vk::Format::eR32Sfloat, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    {
        vk::ImageViewCreateInfo vci{};
        vci.setImage(gridImage_.handle()).setViewType(vk::ImageViewType::e2D)
           .setFormat(vk::Format::eR32Sfloat)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        gridView_ = device.createImageViewUnique(vci);
    }

    // Build colormap LUT as RGBA8 1D texture (256 entries).
    constexpr uint32_t kLutSize = 256;
    std::vector<uint8_t> lut(kLutSize * 4);
    for (uint32_t i = 0; i < kLutSize; ++i) {
        float t = float(i) / float(kLutSize - 1);
        auto c = cmap.sample(t);
        lut[i*4+0] = uint8_t(c.r * 255.0f);
        lut[i*4+1] = uint8_t(c.g * 255.0f);
        lut[i*4+2] = uint8_t(c.b * 255.0f);
        lut[i*4+3] = uint8_t(c.a * 255.0f);
    }
    {
        core::ImageDesc idesc{};
        idesc.format = vk::Format::eR8G8B8A8Unorm;
        idesc.extent = vk::Extent2D{kLutSize, 1};
        idesc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        idesc.tiling = vk::ImageTiling::eOptimal;
        cmapImage_ = core::Image(allocator, idesc);

        core::BufferDesc bdesc{};
        bdesc.size = lut.size();
        bdesc.usage = core::BufferUsage::Staging;
        bdesc.hostVisible = true;
        core::Buffer staging(allocator, bdesc);
        std::memcpy(staging.mappedData(), lut.data(), lut.size());

        core::OneTimeCommands cmd(device, pool, queue);
        vk::BufferImageCopy region{};
        region.setBufferOffset(0)
              .setBufferRowLength(kLutSize)
              .setBufferImageHeight(1)
              .setImageSubresource(vk::ImageSubresourceLayers{}
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
              .setImageOffset({0,0,0})
              .setImageExtent({kLutSize, 1, 1});
        core::Image::transitionLayout(cmd.handle(), cmapImage_.handle(),
            vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal);
        cmd.handle().copyBufferToImage(staging.handle(), cmapImage_.handle(),
            vk::ImageLayout::eTransferDstOptimal, region);
        core::Image::transitionLayout(cmd.handle(), cmapImage_.handle(),
            vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    {
        vk::ImageViewCreateInfo vci{};
        vci.setImage(cmapImage_.handle()).setViewType(vk::ImageViewType::e2D)
           .setFormat(vk::Format::eR8G8B8A8Unorm)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        cmapView_ = device.createImageViewUnique(vci);
    }

    // Upload fullscreen quad vertices
    {
        core::BufferDesc bdesc{};
        bdesc.size = sizeof(kQuad);
        bdesc.usage = core::BufferUsage::Vertex;
        quadBuffer_ = core::Buffer(allocator, bdesc);
        quadBuffer_.upload(device, queue, pool, std::as_bytes(std::span{kQuad, 12}));
    }

    // Update descriptor set
    vk::DescriptorImageInfo gridInfo{};
    gridInfo.setSampler(sampler_.get())
            .setImageView(gridView_.get())
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::DescriptorImageInfo cmapInfo{};
    cmapInfo.setSampler(sampler_.get())
            .setImageView(cmapView_.get())
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet writes[2];
    writes[0].setDstSet(descSet_).setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
             .setImageInfo(gridInfo);
    writes[1].setDstSet(descSet_).setDstBinding(1)
             .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
             .setImageInfo(cmapInfo);
    device.updateDescriptorSets(writes, {});

    // Store value range for draw
    valueMin_ = grid.valueRange.min;
    valueMax_ = grid.valueRange.max;
    gridXRange_ = grid.xRange;
    gridYRange_ = grid.yRange;
}

void HeatmapRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                           const plot::Transform2D& transform) const {
    if (!inited_ || !quadBuffer_.handle()) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float gridXMin, gridXMax, gridYMin, gridYMax;
        float valueMin, valueMax;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.gridXMin = gridXRange_.min;
    pc.gridXMax = gridXRange_.max;
    pc.gridYMin = gridYRange_.min;
    pc.gridYMax = gridYRange_.max;
    pc.valueMin = valueMin_;
    pc.valueMax = valueMax_;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout_.get(), 0, descSet_, {});
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

    std::array<vk::Buffer, 1> buf = { quadBuffer_.handle() };
    std::array<vk::DeviceSize, 1> off = {0};
    cmd.bindVertexBuffers(0, buf, off);
    cmd.draw(6, 1, 0, 0);
}

} // namespace volcano::render::primitives
