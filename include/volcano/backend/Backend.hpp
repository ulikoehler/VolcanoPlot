// volcano/backend/Backend.hpp — backend interface + factory
#pragma once

#include <volcano/core/Instance.hpp>
#include <volcano/core/Device.hpp>
#include <volcano/core/Allocator.hpp>
#include <volcano/core/CommandPool.hpp>

#include <vulkan/vulkan.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

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

/// Backend-agnostic raw input event (windowing-layer level). The render
/// layer translates these into plot::Event for Figure::dispatch.
struct InputEvent {
    enum class Type {
        ButtonPress, ButtonRelease, Motion, Scroll,
        KeyPress, KeyRelease, TextInput, Resize, Quit,
    };
    Type type = Type::Motion;
    /// Canvas pixel position (top-left origin, Y-down).
    float x = 0.0f, y = 0.0f;
    /// Mouse button: 1=left, 2=middle, 3=right.
    int button = 0;
    /// Pressed-button bitmask (bit0=left, bit1=middle, bit2=right).
    int buttons = 0;
    bool dblclick = false;
    /// Scroll wheel steps (+up / -down).
    float step = 0.0f;
    /// ASCII key for letters/digits/punctuation (lowercased), 0 otherwise.
    char key = 0;
    /// Raw platform keycode (for named keys).
    uint32_t keycode = 0;
    /// UTF-8 text for TextInput events.
    std::string text;
    bool shift = false, ctrl = false, alt = false;
    /// New size for Resize events.
    uint32_t width = 0, height = 0;
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

    /// Blitting (mpl canvas.copy_from_bbox / restore_region):
    /// snapshot the current color attachment for later restore.
    /// Returns false when unsupported (MSAA, swapchain, ...).
    virtual bool blitCapture() { return false; }
    /// True once blitCapture() has stored a background.
    [[nodiscard]] virtual bool blitCaptured() const { return false; }
    /// Begin a frame that loads the captured background instead of
    /// clearing (loadOp=eLoad). Falls back to beginFrame() when blitting
    /// isn't supported.
    virtual vk::CommandBuffer beginFrameLoad() { return beginFrame(); }

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

    /// Drain queued input events (screen backends translate windowing
    /// events during pollEvents; headless returns empty).
    virtual std::vector<InputEvent> takeEvents() { return {}; }

    /// Framebuffer clear color for the next beginFrame() (figure
    /// facecolor; alpha 0 = transparent export).
    virtual void setClearColor(float r, float g, float b, float a) {
        clearColor_ = {r, g, b, a};
    }

    /// Resize the render target (mpl FigureCanvasBase.resize /
    /// fig.set_size_inches). Headless recreates the framebuffer; screen
    /// backends ignore this (the window drives the extent).
    virtual void resize(uint32_t width, uint32_t height) {
        (void)width; (void)height;
    }

    /// Window title (screen only; no-op for headless).
    virtual void setWindowTitle(std::string_view) {}
    /// Toggle fullscreen (screen only; no-op for headless).
    virtual void toggleFullscreen() {}

protected:
    std::array<float, 4> clearColor_{1.0f, 1.0f, 1.0f, 1.0f};
};

/// Factory: create a screen backend (GLFW window + swapchain).
std::unique_ptr<IBackend> createScreenBackend(const BackendDesc& desc);

/// Factory: create a headless offscreen backend.
std::unique_ptr<IBackend> createHeadlessBackend(const BackendDesc& desc);

/// Find a supported depth format for the given physical device.
/// Tries D32Sfloat, D32SfloatS8Uint, D24UnormS8Uint in order.
[[nodiscard]] vk::Format findDepthFormat(vk::PhysicalDevice phys);

} // namespace volcano::backend
