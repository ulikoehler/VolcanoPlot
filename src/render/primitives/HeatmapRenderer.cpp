// volcano/render/primitives/HeatmapRenderer.cpp
#include "volcano/render/primitives/HeatmapRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/CommandBuffer.hpp>
#include <volcano/plot/Transform.hpp>
#include "../shaders/TransformGlsl.hpp"
#include <array>
#include <format>
#include <stdexcept>
#include <string>

namespace volcano::render::primitives {

namespace {

constexpr const char* kVertHead = R"(
#version 460
layout(location = 0) in vec2 a_pos;  // fullscreen quad [-1,1]

layout(push_constant) uniform PC {
    vec4 u_viewMinSpan;  // xy = min, zw = span (display space)
    vec4 u_gridRange;    // xy = xRange, zw = yRange (data coords of grid)
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
    vec2 u_valueRange;   // min, max of scalar values
} pc;

layout(location = 0) out vec2 v_uv;  // texture coords [0,1]
)";

constexpr const char* kVertMain = R"(
void main() {
    // u_gridRange = (xMin, xMax, yMin, yMax)
    // Map NDC quad position to data coords within the grid range.
    vec2 data = vec2(pc.u_gridRange.x, pc.u_gridRange.z) +
                (a_pos * 0.5 + 0.5) * vec2(pc.u_gridRange.y - pc.u_gridRange.x,
                                           pc.u_gridRange.w - pc.u_gridRange.z);
    // Apply scales + projection, then map display coords to NDC.
    vec2 p = projFwd(vec2(scaleFwd(data.x, pc.u_scaleX.xyz),
                          scaleFwd(data.y, pc.u_scaleY.xyz)),
                     pc.u_proj.xyz);
    vec2 ndc = (p - pc.u_viewMinSpan.xy) / pc.u_viewMinSpan.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    // UV into grid texture: flip Y for Vulkan texture origin.
    // u_proj.w > 0.5 → origin='lower' (row 0 at the bottom).
    v_uv = vec2(a_pos.x * 0.5 + 0.5,
                pc.u_proj.w > 0.5 ? a_pos.y * 0.5 + 0.5
                                  : 0.5 - a_pos.y * 0.5);
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
    vec4 u_scaleX;
    vec4 u_scaleY;
    vec4 u_proj;
    vec2 u_valueRange;
    float u_interp;  // 0 = nearest, 1 = bilinear, 2 = bicubic
    float u_interpPad;
} pc;

// Catmull-Rom bicubic weights for fractional offset f (taps at -1..2).
vec4 crWeights(float f) {
    float f2 = f * f;
    float f3 = f2 * f;
    return vec4(-0.5 * f3 + f2 - 0.5 * f,
                 1.5 * f3 - 2.5 * f2 + 1.0,
                -1.5 * f3 + 2.0 * f2 + 0.5 * f,
                 0.5 * f3 - 0.5 * f2);
}

float bicubicSample(vec2 uv) {
    ivec2 sz = textureSize(u_grid, 0);
    vec2 tc = uv * vec2(sz) - 0.5;
    vec2 f = fract(tc);
    ivec2 base = ivec2(floor(tc));
    vec4 wx = crWeights(f.x);
    vec4 wy = crWeights(f.y);
    float acc = 0.0;
    for (int j = 0; j < 4; ++j) {
        int ty = clamp(base.y + j - 1, 0, sz.y - 1);
        for (int i = 0; i < 4; ++i) {
            int tx = clamp(base.x + i - 1, 0, sz.x - 1);
            acc += wx[i] * wy[j] * texelFetch(u_grid, ivec2(tx, ty), 0).r;
        }
    }
    return acc;
}

void main() {
    // nearest/bilinear differ only by the bound sampler's filter mode;
    // bicubic fetches texels explicitly.
    float v = pc.u_interp > 1.5 ? bicubicSample(v_uv)
                                : texture(u_grid, v_uv).r;
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
    auto vertSrc = std::string(kVertHead) + shaders::kScaleFn +
                   shaders::kProjFn + kVertMain;
    auto v = core::ShaderModule::compileGlsl(vertSrc, "vert");
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
       .setOffset(0).setSize(sizeof(float) * 24);
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

    // Nearest sampler for the data grid (matplotlib imshow/pcolormesh
    // draw discrete cells, not interpolated values).
    sci.setMagFilter(vk::Filter::eNearest).setMinFilter(vk::Filter::eNearest)
       .setMipmapMode(vk::SamplerMipmapMode::eNearest);
    samplerNearest_ = device.createSamplerUnique(sci);

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

    // mpl imshow interpolation: bilinear binds the linear sampler;
    // antialiased/auto are approximated by bilinear (mpl maps them to
    // its auto/hanning resampler); bicubic is done with texelFetch in
    // the shader so the sampler filter is irrelevant.
    if (grid.interpolation == "bilinear" || grid.interpolation == "antialiased" ||
        grid.interpolation == "hanning") {
        interpMode_ = 1;
    } else if (grid.interpolation == "bicubic") {
        interpMode_ = 2;
    } else if (grid.interpolation == "nearest" || grid.interpolation == "none" ||
               grid.interpolation.empty()) {
        interpMode_ = 0;
    } else {
        throw std::invalid_argument(
            std::format("HeatmapRenderer: unsupported interpolation '{}'",
                        grid.interpolation));
    }

    // Update descriptor set
    vk::DescriptorImageInfo gridInfo{};
    gridInfo.setSampler(interpMode_ == 1 ? sampler_.get()
                                         : samplerNearest_.get())
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
    originLower_ = grid.origin == "lower";
}

void HeatmapRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                           const plot::Transform2D& transform) const {
    if (!inited_ || !quadBuffer_.handle()) return;

    struct PC {
        float viewMinX, viewMinY, viewSpanX, viewSpanY;
        float gridXMin, gridXMax, gridYMin, gridYMax;
        float sxCode, sxP1, sxP2, sxPad;
        float syCode, syP1, syP2, syPad;
        float prCode, thetaOff, thetaDir, prPad;
        float valueMin, valueMax;
        float interp, interpPad;
    } pc;
    pc.viewMinX = transform.view.x.min;
    pc.viewMinY = transform.view.y.min;
    pc.viewSpanX = transform.view.x.span();
    pc.viewSpanY = transform.view.y.span();
    pc.gridXMin = gridXRange_.min;
    pc.gridXMax = gridXRange_.max;
    pc.gridYMin = gridYRange_.min;
    pc.gridYMax = gridYRange_.max;
    pc.sxCode = static_cast<float>(static_cast<int>(transform.codeX()));
    pc.sxP1 = transform.scaleX.param1;
    pc.sxP2 = transform.scaleX.param2;
    pc.syCode = static_cast<float>(static_cast<int>(transform.codeY()));
    pc.syP1 = transform.scaleY.param1;
    pc.syP2 = transform.scaleY.param2;
    pc.prCode = static_cast<float>(static_cast<int>(transform.projection.kind));
    pc.thetaOff = transform.projection.thetaOffset;
    pc.thetaDir = transform.projection.thetaDir;
    pc.prPad = originLower_ ? 1.0f : 0.0f;
    pc.valueMin = valueMin_;
    pc.valueMax = valueMax_;
    pc.interp = static_cast<float>(interpMode_);
    pc.interpPad = 0.0f;

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
