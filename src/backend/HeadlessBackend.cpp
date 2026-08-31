// volcano/backend/HeadlessBackend.cpp
#include "volcano/backend/HeadlessBackend.hpp"

#include <volcano/core/Buffer.hpp>
#include <volcano/core/CommandBuffer.hpp>

#include <stdexcept>

namespace volcano::backend {

HeadlessBackend::HeadlessBackend(const BackendDesc& desc) : desc_(desc) {
    extent_ = vk::Extent2D{desc.width, desc.height};
    colorFormat_ = (desc.colorFormat == vk::Format::eUndefined)
                       ? vk::Format::eR8G8B8A8Unorm : desc.colorFormat;
    samples_ = desc.samples;

    // Instance — no surface, no validation by default.
    core::InstanceDesc idesc{};
    idesc.applicationName = "VolcanoPlot Headless";
    idesc.enableValidation = desc.enableValidation;
    ctx_.instance = core::Instance(idesc);

    // Physical device — no surface.
    ctx_.physical = core::PhysicalDevice(ctx_.instance.handle(), nullptr);

    // Cap MSAA to device-supported samples.
    auto props = ctx_.physical.properties();
    auto maxSamples = props.limits.framebufferColorSampleCounts;
    if (!(maxSamples & samples_)) {
        for (auto s : {vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
                       vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2,
                       vk::SampleCountFlagBits::e1}) {
            if (maxSamples & s) { samples_ = s; break; }
        }
    }

    core::DeviceDesc ddesc{};
    ddesc.hasSurface = false;
    ddesc.features.features.wideLines = VK_TRUE;
    ctx_.device = core::Device(ctx_.physical, ddesc);
    ctx_.allocator = core::Allocator(ctx_.instance.handle(), ctx_.physical.handle(), ctx_.device.handle());
    ctx_.graphicsPool = core::CommandPool(ctx_.device.handle(), ctx_.device.graphicsFamily(),
                                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    ctx_.computePool = core::CommandPool(ctx_.device.handle(), ctx_.device.computeFamily(),
                                         vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    createRenderPass();
    createFramebuffer();
    createCommandBuffer();
    renderFence_ = ctx_.device.handle().createFenceUnique({});
}

HeadlessBackend::~HeadlessBackend() {
    ctx_.device.waitIdle();
}

void HeadlessBackend::createRenderPass() {
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;
    depthFormat_ = findDepthFormat(ctx_.physical.handle());

    vk::AttachmentDescription colorAtt{};
    colorAtt.setFormat(colorFormat_)
        .setSamples(samples_)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(msaa ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eTransferSrcOptimal);

    vk::AttachmentDescription resolveAtt{};
    resolveAtt.setFormat(colorFormat_)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eTransferSrcOptimal);

    vk::AttachmentDescription depthAtt{};
    depthAtt.setFormat(depthFormat_)
        .setSamples(samples_)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference colorRef{};
    colorRef.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference resolveRef{};
    resolveRef.setAttachment(1).setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    // Depth attachment reference index depends on whether resolve is present.
    uint32_t depthIdx = msaa ? 2 : 1;
    vk::AttachmentReference depthRef{};
    depthRef.setAttachment(depthIdx).setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::SubpassDescription sub{};
    sub.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
       .setColorAttachments(colorRef)
       .setPDepthStencilAttachment(&depthRef);
    if (msaa) sub.setResolveAttachments(resolveRef);

    vk::SubpassDependency deps[2];
    deps[0].setSrcSubpass(VK_SUBPASS_EXTERNAL)
       .setDstSubpass(0)
       .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                      | vk::PipelineStageFlagBits::eEarlyFragmentTests)
       .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput
                      | vk::PipelineStageFlagBits::eEarlyFragmentTests)
       .setSrcAccessMask(vk::AccessFlagBits::eNone)
       .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite
                       | vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    deps[1].setSrcSubpass(0)
       .setDstSubpass(VK_SUBPASS_EXTERNAL)
       .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
       .setDstStageMask(vk::PipelineStageFlagBits::eTransfer)
       .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
       .setDstAccessMask(vk::AccessFlagBits::eTransferRead);

    std::vector<vk::AttachmentDescription> atts = { colorAtt };
    if (msaa) atts.push_back(resolveAtt);
    atts.push_back(depthAtt);

    vk::RenderPassCreateInfo ci{};
    ci.setAttachments(atts).setSubpasses(sub).setDependencies(deps);
    renderPass_ = ctx_.device.handle().createRenderPassUnique(ci);
}

void HeadlessBackend::createFramebuffer() {
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;

    // Resolve/single-sample color image — host-visible for readback.
    core::ImageDesc cdesc{};
    cdesc.format = colorFormat_;
    cdesc.extent = extent_;
    cdesc.usage = vk::ImageUsageFlagBits::eColorAttachment |
                  vk::ImageUsageFlagBits::eTransferSrc;
    colorImage_ = core::Image(ctx_.allocator.handle(), cdesc);
    vk::ImageViewCreateInfo cvi{};
    cvi.setImage(colorImage_.handle())
       .setViewType(vk::ImageViewType::e2D)
       .setFormat(colorFormat_)
       .setSubresourceRange(vk::ImageSubresourceRange{}
           .setAspectMask(vk::ImageAspectFlagBits::eColor)
           .setBaseMipLevel(0).setLevelCount(1)
           .setBaseArrayLayer(0).setLayerCount(1));
    colorView_ = ctx_.device.handle().createImageViewUnique(cvi);

    if (msaa) {
        core::ImageDesc mdesc{};
        mdesc.format = colorFormat_;
        mdesc.extent = extent_;
        mdesc.samples = samples_;
        mdesc.usage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransientAttachment;
        msaaImage_ = core::Image(ctx_.allocator.handle(), mdesc);
        vk::ImageViewCreateInfo mvi{};
        mvi.setImage(msaaImage_.handle())
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(colorFormat_)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        msaaView_ = ctx_.device.handle().createImageViewUnique(mvi);
    }

    // Depth image (matches sample count for MSAA).
    if (depthFormat_ != vk::Format::eUndefined) {
        core::ImageDesc ddesc{};
        ddesc.format = depthFormat_;
        ddesc.extent = extent_;
        ddesc.samples = samples_;
        ddesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthImage_ = core::Image(ctx_.allocator.handle(), ddesc);
        vk::ImageViewCreateInfo dvi{};
        dvi.setImage(depthImage_.handle())
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(depthFormat_)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eDepth)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        depthView_ = ctx_.device.handle().createImageViewUnique(dvi);
    }

    std::vector<vk::ImageView> attachments;
    if (msaa) attachments = { msaaView_.get(), colorView_.get() };
    else      attachments = { colorView_.get() };
    if (depthView_) attachments.push_back(depthView_.get());

    vk::FramebufferCreateInfo ci{};
    ci.setRenderPass(renderPass_.get())
       .setAttachments(attachments)
       .setWidth(extent_.width)
       .setHeight(extent_.height)
       .setLayers(1);
    framebuffer_ = ctx_.device.handle().createFramebufferUnique(ci);
}

void HeadlessBackend::createCommandBuffer() {
    vk::CommandBufferAllocateInfo ai{};
    ai.setCommandPool(ctx_.graphicsPool.handle())
       .setLevel(vk::CommandBufferLevel::ePrimary)
       .setCommandBufferCount(1);
    auto cbs = ctx_.device.handle().allocateCommandBuffersUnique(ai);
    commandBuffer_ = std::move(cbs.front());
}

vk::CommandBuffer HeadlessBackend::beginFrame() {
    auto cb = commandBuffer_.get();
    cb.reset();
    vk::CommandBufferBeginInfo bi{};
    bi.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cb.begin(bi);

    std::array<vk::ClearValue, 2> clears{};
    clears[0].color.setFloat32({1.0f, 1.0f, 1.0f, 1.0f});
    clears[1].depthStencil.setDepth(1.0f).setStencil(0);
    vk::RenderPassBeginInfo rpi{};
    rpi.setRenderPass(renderPass_.get())
       .setFramebuffer(framebuffer_.get())
       .setRenderArea(vk::Rect2D{}.setOffset({0,0}).setExtent(extent_))
       .setClearValues(clears);
    cb.beginRenderPass(rpi, vk::SubpassContents::eInline);
    frameBegun_ = true;
    return cb;
}

void HeadlessBackend::endFrame() {
    if (!frameBegun_) return;
    auto cb = commandBuffer_.get();
    cb.endRenderPass();
    cb.end();
    frameBegun_ = false;

    vk::SubmitInfo si{};
    si.setCommandBuffers(cb);
    ctx_.device.graphicsQueue().submit(si, renderFence_.get());
    ctx_.device.waitIdle();
}

std::vector<uint8_t> HeadlessBackend::readbackRgba8() {
    auto dev = ctx_.device.handle();
    auto queue = ctx_.device.graphicsQueue();
    auto pool = ctx_.graphicsPool.handle();

    // Create a host-visible staging buffer.
    vk::DeviceSize size = vk::DeviceSize(extent_.width) * extent_.height * 4;
    core::BufferDesc bdesc{};
    bdesc.size = size;
    bdesc.usage = core::BufferUsage::Staging;
    bdesc.hostVisible = true;
    bdesc.hostCached = true;
    core::Buffer staging(ctx_.allocator.handle(), bdesc);

    // Copy image → buffer via a one-time command buffer.
    // The color image is already in eTransferSrcOptimal (render pass final layout).
    {
        core::OneTimeCommands cmd(dev, pool, queue);

        // Ensure render pass writes are visible to the transfer read.
        vk::ImageMemoryBarrier barrier{};
        barrier.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
               .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
               .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
               .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
               .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
               .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
               .setImage(colorImage_.handle())
               .setSubresourceRange(vk::ImageSubresourceRange{}
                   .setAspectMask(vk::ImageAspectFlagBits::eColor)
                   .setBaseMipLevel(0).setLevelCount(1)
                   .setBaseArrayLayer(0).setLayerCount(1));
        cmd.handle().pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier);

        vk::BufferImageCopy region{};
        region.setBufferOffset(0)
              .setBufferRowLength(extent_.width)
              .setBufferImageHeight(extent_.height)
              .setImageSubresource(vk::ImageSubresourceLayers{}
                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                  .setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
              .setImageOffset({0,0,0})
              .setImageExtent({extent_.width, extent_.height, 1});
        cmd.handle().copyImageToBuffer(colorImage_.handle(),
                                       vk::ImageLayout::eTransferSrcOptimal,
                                       staging.handle(), region);
    } // ~OneTimeCommands submits and waits for completion.

    // Map and copy out.
    std::vector<uint8_t> out(size);
    std::memcpy(out.data(), staging.mappedData(), size);
    return out;
}

} // namespace volcano::backend
