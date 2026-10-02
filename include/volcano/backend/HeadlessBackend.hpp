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
    /// Backend on a shared GPU context: framebuffers/render passes are
    /// private, instance/device/allocator are shared.
    HeadlessBackend(const BackendDesc& desc,
                    std::shared_ptr<GpuContext> shared);
    ~HeadlessBackend() override;

    bool pollEvents() override { return true; }
    std::unique_ptr<render::Cmd> beginFrame() override;
    std::unique_ptr<render::Cmd> beginFrameLoad() override;
    void endFrame() override;
    std::vector<uint8_t> readbackRgba8() override;
    render::GpuServices& gpu() override;

    bool blitCapture() override;
    [[nodiscard]] bool blitCaptured() const override { return blitCaptured_; }

    /// Recreate the color/depth targets at a new extent. The render
    /// pass is extent-independent and stays alive, so renderer
    /// pipelines remain valid.
    void resize(uint32_t width, uint32_t height) override;

    // Vulkan-specific accessors (native only — not part of IBackend).
    [[nodiscard]] GpuContext& context() noexcept { return ctx_; }
    [[nodiscard]] const GpuContext& context() const noexcept { return ctx_; }
    [[nodiscard]] plot::Extent2D extent() const noexcept override {
        return {extent_.width, extent_.height};
    }
    [[nodiscard]] vk::Format colorFormat() const noexcept { return colorFormat_; }
    [[nodiscard]] vk::SampleCountFlagBits sampleCount() const noexcept { return samples_; }
    [[nodiscard]] vk::RenderPass renderPass() const noexcept { return renderPass_.get(); }
    [[nodiscard]] vk::Format depthFormat() const noexcept { return depthFormat_; }

private:
    void createRenderPass();
    void createFramebuffer();
    void createCommandBuffer();
    /// Build a render pass; `colorLoad`/`colorInitial` configure the
    /// color attachment (clear for fresh frames, load for blit frames).
    vk::UniqueRenderPass makeRenderPass(vk::AttachmentLoadOp colorLoad,
                                        vk::ImageLayout colorInitial);

    /// Instance/device/pool init when no shared context is given.
    void createContext();
    /// Render pass + framebuffer + command buffer on the (possibly
    /// shared) context.
    void createTargets();

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
    core::Image depthImage_;
    vk::UniqueImageView depthView_;
    vk::Format depthFormat_ = vk::Format::eUndefined;
    vk::UniqueFramebuffer framebuffer_;

    vk::UniqueCommandBuffer commandBuffer_;
    vk::UniqueFence renderFence_;
    bool frameBegun_ = false;

    /// Blit state: eLoad-variant render pass + snapshot image.
    vk::UniqueRenderPass renderPassLoad_;
    core::Image blitImage_;
    bool blitCaptured_ = false;

    /// GPU service facade (lazily created by gpu()).
    std::unique_ptr<render::GpuServices> gpuServices_;
};

} // namespace volcano::backend
