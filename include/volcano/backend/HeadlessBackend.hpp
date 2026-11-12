// volcano/backend/HeadlessBackend.hpp — offscreen render target
#pragma once

#include "volcano/backend/Backend.hpp"

#include <volcano/core/Instance.hpp>
#include <volcano/core/Image.hpp>
#include <volcano/core/Buffer.hpp>

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
    void beginRenderPass() override;
    void endFrame() override;
    std::vector<uint8_t> readbackRgba8() override;
    size_t readbackRgba8Into(uint8_t* dst, size_t dstLen) override;
    const uint8_t* rgba8View(size_t* outLen) override;
    void setDeferredReadback(bool on) override { deferredReadback_ = on; }
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
    /// Lazily (re)create the persistent readback staging buffer to
    /// fit the current extent.
    void ensureStaging(uint32_t slot);
    /// Poll a slot's fence until its submission completes.
    void waitSlot(uint32_t slot);
    /// Slot of the newest *presentable* frame: the latest submission
    /// once signaled, else (deferred only) the previous one after a
    /// short wait. -1 when nothing has been submitted yet.
    int presentSlot();

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

    /// Ping-pong frame slots: command buffer + fence + readback staging
    /// per slot. Deferred readback records frame N into slot N%2 while
    /// slot (N-1)%2 presents the previous frame — the caller never
    /// blocks on GPU completion. Sync mode uses the same slots but
    /// waits the just-submitted fence before endFrame returns.
    std::array<vk::UniqueCommandBuffer, 2> commandBuffers_;
    std::array<vk::UniqueFence, 2> renderFences_;
    uint32_t frameSlot_ = 0;
    uint64_t submitted_ = 0;
    bool deferredReadback_ = false;
    bool frameBegun_ = false;
    /// Which pass beginRenderPass() opens: clear (false) or the
    /// blit-load variant (true). Set by beginFrame*/beginFrameLoad.
    bool pendingLoad_ = false;
    /// True once beginRenderPass() has opened the pass this frame —
    /// endFrame() auto-begins for frames that never drew.
    bool passBegun_ = false;

    /// Persistent host-visible readback target — the image→buffer
    /// copy is recorded into `commandBuffers_[frameSlot_]` inside
    /// endFrame(), so a frame is ONE submit + ONE fence wait instead
    /// of a second one-time submit + queue wait + per-frame VMA alloc.
    /// Double-buffered so a deferred frame's copy target is never the
    /// buffer the caller is still reading.
    std::array<core::Buffer, 2> stagingBuffers_;

    /// Blit state: eLoad-variant render pass + snapshot image.
    vk::UniqueRenderPass renderPassLoad_;
    core::Image blitImage_;
    bool blitCaptured_ = false;

    /// GPU service facade (lazily created by gpu()).
    std::unique_ptr<render::GpuServices> gpuServices_;
};

} // namespace volcano::backend
