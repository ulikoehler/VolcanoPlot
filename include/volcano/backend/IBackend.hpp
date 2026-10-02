// volcano/backend/IBackend.hpp — backend-neutral interface
//
// This header is part of the WASM build: it must stay free of Vulkan
// types. GpuContext / BackendDesc / the native factories live in
// backend/Backend.hpp (native only). The WebGPU backend (src/web/
// WebBackend) implements this interface with an OpStream-producing
// GpuServices.
#pragma once

#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace volcano::render { class GpuServices; }

namespace volcano::backend {

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

/// Abstract backend. Native implementations: ScreenBackend (GLFW +
/// swapchain) and HeadlessBackend (offscreen) in backend/Backend.hpp.
/// WebGPU: web::WebBackend in src/web/.
class IBackend {
public:
    virtual ~IBackend() = default;

    /// Poll window events (screen) / no-op (headless). Returns false when the
    /// window should close.
    virtual bool pollEvents() = 0;

    /// Acquire the next frame's color target and return the frame's
    /// command context (a begun Vulkan command buffer wrapped in VkCmd;
    /// an OpStream recorder on the web backend).
    virtual std::unique_ptr<render::Cmd> beginFrame() = 0;

    /// End the frame: finish command recording, submit, and present
    /// (screen) / resolve (headless) / flush the op stream (web).
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
    virtual std::unique_ptr<render::Cmd> beginFrameLoad() {
        return beginFrame();
    }

    /// GPU service factory / shared renderers. Same instance for the
    /// backend's lifetime.
    virtual render::GpuServices& gpu() = 0;

    [[nodiscard]] virtual plot::Extent2D extent() const noexcept = 0;

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

} // namespace volcano::backend
