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

namespace {
std::shared_ptr<GpuContext> sharedGpuContextImpl(bool extended) {
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
    c->extendedSharing = extended;
    core::InstanceDesc idesc{};
    idesc.applicationName = "VolcanoPlot Headless";
    if (extended) {
        // A foreign Vulkan consumer (wgpu-hal wrapping this instance)
        // needs the surface + device-property extensions it checks
        // for to be enabled up front. Availability-filtered inside
        // core::Instance, so listing all platforms is safe.
        // (string literals — the VK_KHR_*_surface macros are gated
        // behind VK_USE_PLATFORM_* defines we don't set)
        idesc.extraExtensions = std::vector<std::string>{
            "VK_KHR_surface",
            "VK_KHR_xcb_surface",
            "VK_KHR_xlib_surface",
            "VK_KHR_wayland_surface",
            "VK_KHR_get_surface_capabilities2",
            "VK_EXT_surface_maintenance1",
            "VK_EXT_swapchain_colorspace",
            "VK_KHR_get_physical_device_properties2",
        };
    }
    c->instance = core::Instance(idesc);
    c->apiVersion = VK_API_VERSION_1_3;
    if (vk::enumerateInstanceVersion(&c->apiVersion) != vk::Result::eSuccess)
        c->apiVersion = VK_API_VERSION_1_3;
    c->physical =
        core::PhysicalDevice(c->instance.handle(), nullptr);
    core::DeviceDesc ddesc{};
    // hasSurface=false suppresses only the auto-swapchain extension —
    // in extended mode swapchain is still requested explicitly
    // because wgpu's swapchain will live on this device.
    ddesc.hasSurface = false;
    if (extended) {
        // Enable every device extension the physical device offers so
        // wgpu's required_device_extensions list is trivially
        // satisfied (device_from_raw accepts a superset), plus every
        // supported feature via a features2 chain — wgpu declares
        // what it will use, we merely must have enabled it.
        ddesc.extensions = c->physical.supportedExtensions();
        static vk::PhysicalDeviceFeatures2 f2;
        static vk::PhysicalDeviceVulkan11Features f11;
        static vk::PhysicalDeviceVulkan12Features f12;
        static vk::PhysicalDeviceVulkan13Features f13;
        f2 = vk::PhysicalDeviceFeatures2{};
        f11 = vk::PhysicalDeviceVulkan11Features{};
        f12 = vk::PhysicalDeviceVulkan12Features{};
        f13 = vk::PhysicalDeviceVulkan13Features{};
        f11.pNext = &f12;
        f12.pNext = &f13;
        f2.pNext = &f11;
        c->physical.handle().getFeatures2(&f2);
        ddesc.pNextChain = &f2;
    } else {
        ddesc.features.features.wideLines = VK_TRUE;
        if (c->physical.handle().getFeatures().shaderFloat64)
            ddesc.features.features.shaderFloat64 = VK_TRUE;
    }
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
} // namespace

std::shared_ptr<GpuContext> sharedGpuContext() {
    return sharedGpuContextImpl(false);
}

std::shared_ptr<GpuContext> sharedGpuContextExtended() {
    return sharedGpuContextImpl(true);
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
