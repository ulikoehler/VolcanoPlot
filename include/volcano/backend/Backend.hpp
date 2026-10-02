// volcano/backend/Backend.hpp — native backend context + factories
//
// NATIVE BUILDS ONLY — pulls in Vulkan types via core/. The
// backend-neutral interface (IBackend, InputEvent) lives in
// backend/IBackend.hpp which also compiles under Emscripten.
#pragma once

#include <volcano/backend/IBackend.hpp>
#ifndef VOLCANO_WEB
#include <volcano/core/Instance.hpp>
#include <volcano/core/Device.hpp>
#include <volcano/core/Allocator.hpp>
#include <volcano/core/CommandPool.hpp>

#include <vulkan/vulkan.hpp>
#endif

#include <memory>
#include <string>

#ifdef VOLCANO_WEB
// Web builds have no GpuContext/factories — WebBackend lives in
// src/web/WebBackend.hpp. BackendDesc exists so shared headers parse.
namespace volcano::backend { struct BackendDesc { uint32_t width = 1280, height = 720; }; }
#else
namespace volcano::backend {

struct BackendDesc {
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string windowTitle = "VolcanoPlot";
    bool enableValidation = false;
    /// MSAA sample count for the color target (1, 2, 4, 8, 16).
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e4;
    /// Format for the color attachment; eUndefined lets the backend pick.
    vk::Format colorFormat = vk::Format::eUndefined;
};

/// Common GPU context shared between backends.
struct GpuContext {
    core::Instance instance;
    core::PhysicalDevice physical;
    core::Device device;
    core::Allocator allocator;
    core::CommandPool graphicsPool;
    core::CommandPool computePool;

    /// A GpuContext whose members share this one's Vulkan objects —
    /// they die with the last holder. Lets many backends run on one
    /// device without paying instance/device creation per backend.
    [[nodiscard]] GpuContext share() const {
        GpuContext c;
        c.instance = instance.share();
        c.physical = physical;
        c.device = device.share();
        c.allocator = allocator.share();
        c.graphicsPool = graphicsPool.share();
        c.computePool = computePool.share();
        return c;
    }
};

/// Factory: create a screen backend (GLFW window + swapchain).
std::unique_ptr<IBackend> createScreenBackend(const BackendDesc& desc);

/// Factory: create a headless offscreen backend.
std::unique_ptr<IBackend> createHeadlessBackend(const BackendDesc& desc);
/// Headless backend on a shared GPU context — skips instance/device
/// creation (the expensive part) and builds only per-backend targets.
std::unique_ptr<IBackend> createHeadlessBackend(
    const BackendDesc& desc, std::shared_ptr<GpuContext> shared);
/// Process-wide GPU context for headless rendering: one
/// vkInstance/vkDevice shared by all headless backends created with
/// the overload above. Created lazily on first call.
std::shared_ptr<GpuContext> sharedGpuContext();

/// Find a supported depth format for the given physical device.
/// Tries D32Sfloat, D32SfloatS8Uint, D24UnormS8Uint in order.
[[nodiscard]] vk::Format findDepthFormat(vk::PhysicalDevice phys);

} // namespace volcano::backend
#endif // !VOLCANO_WEB
