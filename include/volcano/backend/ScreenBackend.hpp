// volcano/backend/ScreenBackend.hpp — GLFW window + Vulkan swapchain
#pragma once

#include "volcano/backend/Backend.hpp"

#include <volcano/core/Instance.hpp>
#include <volcano/core/Image.hpp>

#include <vulkan/vulkan.hpp>

#include <memory>

struct GLFWwindow;

namespace volcano::backend {

class ScreenBackend : public IBackend {
public:
    explicit ScreenBackend(const BackendDesc& desc);
    ~ScreenBackend() override;

    bool pollEvents() override;
    std::vector<InputEvent> takeEvents() override;
    void setWindowTitle(std::string_view title) override;
    void toggleFullscreen() override;
    vk::CommandBuffer beginFrame() override;
    void endFrame() override;

    [[nodiscard]] GpuContext& context() noexcept override { return ctx_; }
    [[nodiscard]] const GpuContext& context() const noexcept override { return ctx_; }
    [[nodiscard]] vk::Extent2D extent() const noexcept override { return extent_; }
    [[nodiscard]] vk::Format colorFormat() const noexcept override { return colorFormat_; }
    [[nodiscard]] vk::SampleCountFlagBits sampleCount() const noexcept override { return samples_; }
    [[nodiscard]] vk::RenderPass renderPass() const noexcept override { return renderPass_.get(); }
    [[nodiscard]] vk::Format depthFormat() const noexcept override { return depthFormat_; }

private:
    void createSurface();
    void createSwapchain();
    void createRenderPass();
    void createFramebuffers();
    void recreateSwapchain();

    // GLFW event plumbing — callbacks trampoline onto these helpers via
    // the window user pointer.
    void onFramebufferSize(int w, int h);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double x, double y);
    void onScroll(double xoff, double yoff);
    void onKey(int key, int scancode, int action, int mods);
    void onChar(unsigned int codepoint);
    [[nodiscard]] int buttonMask() const;
    static void fillMods(InputEvent& ev, int mods);
    static void cbFramebufferSize(GLFWwindow*, int, int);
    static void cbMouseButton(GLFWwindow*, int, int, int);
    static void cbCursorPos(GLFWwindow*, double, double);
    static void cbScroll(GLFWwindow*, double, double);
    static void cbKey(GLFWwindow*, int, int, int, int);
    static void cbChar(GLFWwindow*, unsigned int);
    static void cbClose(GLFWwindow*);

    BackendDesc desc_;
    GLFWwindow* window_ = nullptr;
    /// Windowed-mode geometry saved while fullscreen is active.
    int savedX_ = 0, savedY_ = 0, savedW_ = 0, savedH_ = 0;
    bool fullscreen_ = false;
    bool closeRequested_ = false;
    /// Double-click detection (GLFW reports no click counts).
    double lastClickTime_ = -1.0;
    int lastClickButton_ = -1;
    float lastClickX_ = -1.0f, lastClickY_ = -1.0f;

    GpuContext ctx_;
    vk::SurfaceKHR surface_;
    vk::UniqueSwapchainKHR swapchain_;
    vk::Format colorFormat_ = vk::Format::eB8G8R8A8Unorm;
    vk::Extent2D extent_{0,0};
    vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1;
    vk::UniqueRenderPass renderPass_;

    std::vector<vk::Image> swapchainImages_;
    std::vector<vk::UniqueImageView> swapchainViews_;
    std::vector<vk::UniqueFramebuffer> framebuffers_;

    // MSAA resolve target + depth
    core::Image msaaColor_;
    vk::UniqueImageView msaaView_;
    core::Image depthImage_;
    vk::UniqueImageView depthView_;
    vk::Format depthFormat_ = vk::Format::eUndefined;

    // Per-frame command buffers + sync
    std::vector<vk::UniqueCommandBuffer> commandBuffers_;
    std::vector<vk::UniqueSemaphore> imageAvailableSem_;
    std::vector<vk::UniqueSemaphore> renderFinishedSem_;
    std::vector<vk::UniqueFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    uint32_t imageIndex_ = 0;
    bool resized_ = false;
    /// Queued input events translated during pollEvents().
    std::vector<InputEvent> pendingEvents_;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace volcano::backend
