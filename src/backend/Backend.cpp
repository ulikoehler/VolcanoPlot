// volcano/backend/Backend.cpp — factory
#include "volcano/backend/Backend.hpp"

#include <mutex>

#ifdef VOLCANO_HAS_SCREEN_BACKEND
#include "volcano/backend/ScreenBackend.hpp"
#endif
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
#include "volcano/backend/HeadlessBackend.hpp"
#endif

namespace volcano::backend {

std::unique_ptr<IBackend> createScreenBackend(const BackendDesc& desc) {
#ifdef VOLCANO_HAS_SCREEN_BACKEND
    return std::make_unique<ScreenBackend>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

std::unique_ptr<IBackend> createHeadlessBackend(const BackendDesc& desc) {
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
    return std::make_unique<HeadlessBackend>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

std::unique_ptr<IBackend> createHeadlessBackend(
    const BackendDesc& desc, std::shared_ptr<GpuContext> shared) {
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
    if (!shared) return std::make_unique<HeadlessBackend>(desc);
    return std::make_unique<HeadlessBackend>(desc, std::move(shared));
#else
    (void)desc; (void)shared;
    return nullptr;
#endif
}

std::shared_ptr<GpuContext> sharedGpuContext() {
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
    // Process-lifetime context, deliberately leaked (heap static —
    // never destructed). Render workers are detached threads that can
    // still be inside Vulkan calls during exit() / static teardown;
    // if the context's instance/device were destroyed then, those
    // threads would call into freed driver state (observed: SIGSEGV
    // inside the driver at process exit). A leaked context keeps the
    // device valid until the kernel reaps the threads.
    static auto* ctx = new std::shared_ptr<GpuContext>();
    static std::mutex mu;
    std::lock_guard lk(mu);
    if (*ctx) return *ctx;
    auto c = std::make_shared<GpuContext>();
    core::InstanceDesc idesc{};
    idesc.applicationName = "VolcanoPlot Headless";
    c->instance = core::Instance(idesc);
    c->physical =
        core::PhysicalDevice(c->instance.handle(), nullptr);
    core::DeviceDesc ddesc{};
    ddesc.hasSurface = false;
    ddesc.features.features.wideLines = VK_TRUE;
    if (c->physical.handle().getFeatures().shaderFloat64)
        ddesc.features.features.shaderFloat64 = VK_TRUE;
    c->device = core::Device(c->physical, ddesc);
    c->allocator = core::Allocator(c->instance.handle(),
                                 c->physical.handle(),
                                 c->device.handle());
    c->graphicsPool = core::CommandPool(
        c->device.handle(), c->device.graphicsFamily(),
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    c->computePool = core::CommandPool(
        c->device.handle(), c->device.computeFamily(),
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    *ctx = c;
    return c;
#else
    return nullptr;
#endif
}

vk::Format findDepthFormat(vk::PhysicalDevice phys) {
    const vk::Format candidates[] = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };
    for (auto fmt : candidates) {
        auto props = phys.getFormatProperties(fmt);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return fmt;
        }
    }
    return vk::Format::eUndefined;
}

} // namespace volcano::backend
