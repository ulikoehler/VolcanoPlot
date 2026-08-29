// volcano/backend/Backend.hpp — backend interface + factory
#pragma once

#include <volcano/core/Instance.hpp>
#include <volcano/core/Device.hpp>
#include <volcano/core/Allocator.hpp>
#include <volcano/core/CommandPool.hpp>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>

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
};

/// Abstract backend. Both ScreenBackend and HeadlessBackend implement this.
class IBackend {
public:
    virtual ~IBackend() = default;

    /// Poll window events (screen) / no-op (headless). Returns false when the
    /// window should close.
    virtual bool pollEvents() = 0;

    /// Acquire the next frame's color image (and resolve target for MSAA).
    /// Returns a command buffer already begun with the render pass.
    virtual vk::CommandBuffer beginFrame() = 0;

    /// End the render pass, submit, and present (screen) / resolve (headless).
    virtual void endFrame() = 0;

    [[nodiscard]] virtual GpuContext& context() noexcept = 0;
    [[nodiscard]] virtual const GpuContext& context() const noexcept = 0;
    [[nodiscard]] virtual vk::Extent2D extent() const noexcept = 0;
    [[nodiscard]] virtual vk::Format colorFormat() const noexcept = 0;
    [[nodiscard]] virtual vk::SampleCountFlagBits sampleCount() const noexcept = 0;
    [[nodiscard]] virtual vk::RenderPass renderPass() const noexcept = 0;
    [[nodiscard]] virtual vk::Format depthFormat() const noexcept = 0;

    /// For headless: read back the rendered color image as RGBA8.
    /// For screen: returns empty (no readback).
    virtual std::vector<uint8_t> readbackRgba8() { return {}; }
};

/// Factory: create a screen backend (SDL3 window + swapchain).
std::unique_ptr<IBackend> createScreenBackend(const BackendDesc& desc);

/// Factory: create a headless offscreen backend.
std::unique_ptr<IBackend> createHeadlessBackend(const BackendDesc& desc);

/// Find a supported depth format for the given physical device.
/// Tries D32Sfloat, D32SfloatS8Uint, D24UnormS8Uint in order.
[[nodiscard]] vk::Format findDepthFormat(vk::PhysicalDevice phys);

} // namespace volcano::backend
