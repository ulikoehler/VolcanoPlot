// volcano/backend/ScreenBackend.cpp
#include "volcano/backend/ScreenBackend.hpp"

#include <volcano/core/CommandBuffer.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <stdexcept>

namespace volcano::backend {

namespace {

vk::SurfaceFormatKHR pickFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Unorm &&
            f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return f;
    }
    return formats.front();
}

vk::PresentModeKHR pickPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    for (auto m : modes) if (m == vk::PresentModeKHR::eMailbox) return m;
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D pickExtent(const vk::SurfaceCapabilitiesKHR& caps, SDL_Window* win) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int w, h;
    SDL_Vulkan_GetDrawableSize(win, &w, &h);
    vk::Extent2D extent;
    extent.width = std::clamp<uint32_t>(w, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

} // namespace

ScreenBackend::ScreenBackend(const BackendDesc& desc) : desc_(desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::format("SDL_Init failed: {}", SDL_GetError()));
    }
    window_ = SDL_CreateWindow(desc.windowTitle.c_str(), desc.width, desc.height,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window_) throw std::runtime_error(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));

    // Instance
    core::InstanceDesc idesc{};
    idesc.applicationName = desc.windowTitle;
    idesc.enableValidation = desc.enableValidation;
    // SDL3 surface extension
    idesc.extraExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__linux__)
    idesc.extraExtensions.push_back("VK_KHR_xlib_surface");
    idesc.extraExtensions.push_back("VK_KHR_xcb_surface");
    idesc.extraExtensions.push_back("VK_KHR_wayland_surface");
#endif
    ctx_.instance = core::Instance(idesc);

    // Surface
    VkSurfaceKHR surf{};
    if (!SDL_Vulkan_CreateSurface(window_, ctx_.instance.handle(), nullptr, &surf)) {
        throw std::runtime_error(std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }
    surface_ = surf;

    // Physical device + logical device
    ctx_.physical = core::PhysicalDevice(ctx_.instance.handle(), surface_);
    core::DeviceDesc ddesc{};
    ddesc.extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    ctx_.device = core::Device(ctx_.physical, ddesc);

    // Allocator
    ctx_.allocator = core::Allocator(ctx_.instance.handle(), ctx_.physical.handle(), ctx_.device.handle());

    // Command pools
    ctx_.graphicsPool = core::CommandPool(ctx_.device.handle(), ctx_.device.graphicsFamily(),
                                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    ctx_.computePool = core::CommandPool(ctx_.device.handle(), ctx_.device.computeFamily(),
                                         vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    // Determine MSAA sample count supported by the device.
    auto props = ctx_.physical.properties();
    auto maxSamples = props.limits.framebufferColorSampleCounts;
    samples_ = desc.samples;
    if (!(maxSamples & samples_)) {
        // Fall back to highest supported <= requested.
        for (auto s : {vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
                       vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2,
                       vk::SampleCountFlagBits::e1}) {
            if (maxSamples & s) { samples_ = s; break; }
        }
    }

    createSwapchain();
    createRenderPass();
    createFramebuffers();

    // Sync objects + command buffers
    vk::CommandBufferAllocateInfo ai{};
    ai.setCommandPool(ctx_.graphicsPool.handle())
       .setLevel(vk::CommandBufferLevel::ePrimary)
       .setCommandBufferCount(MAX_FRAMES_IN_FLIGHT);
    auto cbs = ctx_.device.handle().allocateCommandBuffersUnique(ai);
    commandBuffers_ = std::move(cbs);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        imageAvailableSem_.push_back(ctx_.device.handle().createSemaphoreUnique({}));
        renderFinishedSem_.push_back(ctx_.device.handle().createSemaphoreUnique({}));
        inFlightFences_.push_back(ctx_.device.handle().createFenceUnique(
            vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled)));
    }
}

ScreenBackend::~ScreenBackend() {
    ctx_.device.waitIdle();
    if (surface_) ctx_.instance.handle().destroySurfaceKHR(surface_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void ScreenBackend::createSurface() {
    // Created in ctor; kept for clarity.
}

void ScreenBackend::createSwapchain() {
    auto phys = ctx_.physical.handle();
    auto caps = phys.getSurfaceCapabilitiesKHR(surface_);
    auto formats = phys.getSurfaceFormatsKHR(surface_);
    auto modes = phys.getSurfacePresentModesKHR(surface_);

    auto fmt = pickFormat(formats);
    colorFormat_ = fmt.format;
    extent_ = pickExtent(caps, window_);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    vk::SwapchainCreateInfoKHR ci{};
    ci.setSurface(surface_)
       .setMinImageCount(imageCount)
       .setImageFormat(fmt.format)
       .setImageColorSpace(fmt.colorSpace)
       .setImageExtent(extent_)
       .setImageArrayLayers(1)
       .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)
       .setImageSharingMode(vk::SharingMode::eExclusive)
       .setPreTransform(caps.currentTransform)
       .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
       .setPresentMode(pickPresentMode(modes))
       .setClipped(true);

    swapchain_ = ctx_.device.handle().createSwapchainKHRUnique(ci);
    swapchainImages_ = ctx_.device.handle().getSwapchainImagesKHR(swapchain_.get());

    swapchainViews_.clear();
    for (auto img : swapchainImages_) {
        vk::ImageViewCreateInfo vci{};
        vci.setImage(img)
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(colorFormat_)
           .setComponents({})
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        swapchainViews_.push_back(ctx_.device.handle().createImageViewUnique(vci));
    }

    // MSAA color target
    if (samples_ != vk::SampleCountFlagBits::e1) {
        core::ImageDesc idesc{};
        idesc.format = colorFormat_;
        idesc.extent = extent_;
        idesc.samples = samples_;
        idesc.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
        msaaColor_ = core::Image(ctx_.allocator.handle(), idesc);
        vk::ImageViewCreateInfo vci{};
        vci.setImage(msaaColor_.handle())
           .setViewType(vk::ImageViewType::e2D)
           .setFormat(colorFormat_)
           .setSubresourceRange(vk::ImageSubresourceRange{}
               .setAspectMask(vk::ImageAspectFlagBits::eColor)
               .setBaseMipLevel(0).setLevelCount(1)
               .setBaseArrayLayer(0).setLayerCount(1));
        msaaView_ = ctx_.device.handle().createImageViewUnique(vci);
    }
}

void ScreenBackend::createRenderPass() {
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;
    vk::AttachmentDescription colorAtt{};
    colorAtt.setFormat(colorFormat_)
        .setSamples(samples_)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(msaa ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentDescription resolveAtt{};
    resolveAtt.setFormat(colorFormat_)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentReference colorRef{};
    colorRef.setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference resolveRef{};
    resolveRef.setAttachment(1).setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    vk::SubpassDescription sub{};
    sub.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
       .setColorAttachments(colorRef);
    if (msaa) sub.setResolveAttachments(resolveRef);

    vk::SubpassDependency dep{};
    dep.setSrcSubpass(VK_SUBPASS_EXTERNAL)
       .setDstSubpass(0)
       .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
       .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
       .setSrcAccessMask(vk::AccessFlagBits::eNone)
       .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    std::vector<vk::AttachmentDescription> atts = { colorAtt };
    if (msaa) atts.push_back(resolveAtt);

    vk::RenderPassCreateInfo ci{};
    ci.setAttachments(atts).setSubpasses(sub).setDependencies(dep);
    renderPass_ = ctx_.device.handle().createRenderPassUnique(ci);
}

void ScreenBackend::createFramebuffers() {
    framebuffers_.clear();
    bool msaa = samples_ != vk::SampleCountFlagBits::e1;
    for (auto& view : swapchainViews_) {
        std::vector<vk::ImageView> attachments;
        if (msaa) {
            attachments = { msaaView_.get(), view.get() };
        } else {
            attachments = { view.get() };
        }
        vk::FramebufferCreateInfo ci{};
        ci.setRenderPass(renderPass_.get())
           .setAttachments(attachments)
           .setWidth(extent_.width)
           .setHeight(extent_.height)
           .setLayers(1);
        framebuffers_.push_back(ctx_.device.handle().createFramebufferUnique(ci));
    }
}

void ScreenBackend::recreateSwapchain() {
    ctx_.device.waitIdle();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    resized_ = false;
}

bool ScreenBackend::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) return false;
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
        if (e.type == SDL_EVENT_WINDOW_RESIZED) resized_ = true;
    }
    return true;
}

vk::CommandBuffer ScreenBackend::beginFrame() {
    if (resized_) recreateSwapchain();

    auto dev = ctx_.device.handle();
    dev.waitForFences(inFlightFences_[currentFrame_].get(), true, UINT64_MAX);
    auto result = dev.acquireNextImageKHR(swapchain_.get(), UINT64_MAX,
                                          imageAvailableSem_[currentFrame_].get(),
                                          VK_NULL_HANDLE, &imageIndex_);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return beginFrame();
    }
    dev.resetFences(inFlightFences_[currentFrame_].get());

    auto cb = commandBuffers_[currentFrame_].get();
    cb.reset();
    vk::CommandBufferBeginInfo bi{};
    bi.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cb.begin(bi);

    std::array<vk::ClearValue, 1> clears{};
    clears[0].color.setFloat32({0.95f, 0.95f, 0.95f, 1.0f});
    vk::RenderPassBeginInfo rpi{};
    rpi.setRenderPass(renderPass_.get())
       .setFramebuffer(framebuffers_[imageIndex_].get())
       .setRenderArea(vk::Rect2D{}.setOffset({0,0}).setExtent(extent_))
       .setClearValues(clears);
    cb.beginRenderPass(rpi, vk::SubpassContents::eInline);
    return cb;
}

void ScreenBackend::endFrame() {
    auto cb = commandBuffers_[currentFrame_].get();
    cb.endRenderPass();
    cb.end();

    vk::SubmitInfo si{};
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    si.setWaitSemaphores(imageAvailableSem_[currentFrame_].get())
       .setWaitDstStageMask(waitStage)
       .setCommandBuffers(cb)
       .setSignalSemaphores(renderFinishedSem_[currentFrame_].get());
    ctx_.device.graphicsQueue().submit(si, inFlightFences_[currentFrame_].get());

    vk::PresentInfoKHR pi{};
    pi.setWaitSemaphores(renderFinishedSem_[currentFrame_].get())
       .setSwapchains(swapchain_.get())
       .setImageIndices(imageIndex_);
    auto result = ctx_.device.graphicsQueue().presentKHR(pi);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || resized_) {
        recreateSwapchain();
    }
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace volcano::backend
