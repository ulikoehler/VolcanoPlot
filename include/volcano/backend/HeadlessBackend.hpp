// volcano/backend/HeadlessBackend.hpp — offscreen render target
#pragma once

#include "volcano/backend/Backend.hpp"

#include <volcano/core/Instance.hpp>
#include <volcano/core/Image.hpp>

#include <vulkan/vulkan.hpp>

namespace volcano::backend {

class HeadlessBackend : public IBackend {
public:
    explicit HeadlessBackend(const BackendDesc& desc);
    ~HeadlessBackend() override;

    bool pollEvents() override { return true; }
    vk::CommandBuffer beginFrame() override;
    void endFrame() override;
    std::vector<uint8_t> readbackRgba8() override;

    [[nodiscard]] GpuContext& context() noexcept override { return ctx_; }
    [[nodiscard]] const GpuContext& context() const noexcept override { return ctx_; }
    [[nodiscard]] vk::Extent2D extent() const noexcept override { return extent_; }
    [[nodiscard]] vk::Format colorFormat() const noexcept override { return colorFormat_; }
    [[nodiscard]] vk::SampleCountFlagBits sampleCount() const noexcept override { return samples_; }
    [[nodiscard]] vk::RenderPass renderPass() const noexcept override { return renderPass_.get(); }

private:
    void createRenderPass();
    void createFramebuffer();
    void createCommandBuffer();

    BackendDesc desc_;
    GpuContext ctx_;
    vk::Format colorFormat_ = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D extent_{0,0};
    vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1;
    vk::UniqueRenderPass renderPass_;

    core::Image colorImage_;
    vk::UniqueImageView colorView_;
    core::Image msaaImage_;
    vk::UniqueImageView msaaView_;
    vk::UniqueFramebuffer framebuffer_;

    vk::UniqueCommandBuffer commandBuffer_;
    vk::UniqueFence renderFence_;
    bool frameBegun_ = false;
};

} // namespace volcano::backend
