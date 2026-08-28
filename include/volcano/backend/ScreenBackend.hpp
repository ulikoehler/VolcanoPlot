// volcano/backend/ScreenBackend.hpp — SDL3 window + Vulkan swapchain
#pragma once

#include "volcano/backend/Backend.hpp"

#include <volcano/core/Instance.hpp>
#include <volcano/core/Image.hpp>

#include <vulkan/vulkan.hpp>

#include <memory>

struct SDL_Window;

namespace volcano::backend {

class ScreenBackend : public IBackend {
public:
    explicit ScreenBackend(const BackendDesc& desc);
    ~ScreenBackend() override;

    bool pollEvents() override;
    vk::CommandBuffer beginFrame() override;
    void endFrame() override;

    [[nodiscard]] GpuContext& context() noexcept override { return ctx_; }
    [[nodiscard]] const GpuContext& context() const noexcept override { return ctx_; }
    [[nodiscard]] vk::Extent2D extent() const noexcept override { return extent_; }
    [[nodiscard]] vk::Format colorFormat() const noexcept override { return colorFormat_; }
    [[nodiscard]] vk::SampleCountFlagBits sampleCount() const noexcept override { return samples_; }
    [[nodiscard]] vk::RenderPass renderPass() const noexcept override { return renderPass_.get(); }

private:
    void createSurface();
    void createSwapchain();
    void createRenderPass();
    void createFramebuffers();
    void recreateSwapchain();

    BackendDesc desc_;
    SDL_Window* window_ = nullptr;
    void* sdlVkSurface_ = nullptr; // VkSurfaceKHR stored as void* to avoid SDL3 vulkan header coupling

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

    // MSAA resolve target + depth (optional)
    core::Image msaaColor_;
    vk::UniqueImageView msaaView_;

    // Per-frame command buffers + sync
    std::vector<vk::UniqueCommandBuffer> commandBuffers_;
    std::vector<vk::UniqueSemaphore> imageAvailableSem_;
    std::vector<vk::UniqueSemaphore> renderFinishedSem_;
    std::vector<vk::UniqueFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    uint32_t imageIndex_ = 0;
    bool resized_ = false;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace volcano::backend
