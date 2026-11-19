// volcano/backend/HeadlessBackend.hpp — offscreen render target
#pragma once

#include "volcano/backend/Backend.hpp"

#include <volcano/core/Instance.hpp>
#include <volcano/core/Image.hpp>
#include <volcano/core/Buffer.hpp>

#include <vulkan/vulkan.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>

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

    // ── Shared-memory display path (vp_render_shm) ──────────────────
    // The frame's staging buffer is consumed directly by a foreign
    // Vulkan consumer (wgpu on the same VkDevice) — no CPU readback.
    // shm mode records a TRANSFER_WRITE→TRANSFER_READ buffer barrier
    // after the image→staging copy and gates slot reuse on the
    // consumer's release signal (shmRelease): a slot's buffer may be
    // rewritten only after the consumer queued its read, because the
    // next render's copy would otherwise race that read in queue.
    void setShmMode(bool on) noexcept { shmMode_ = on; }
    /// Staging buffer backing a presented slot (valid until resize).
    [[nodiscard]] vk::Buffer stagingBuffer(uint32_t slot) const {
        return stagingBuffers_[slot & 1].handle();
    }
    /// Re-create epoch of a slot's staging buffer (bumps on realloc).
    [[nodiscard]] uint32_t stagingGeneration(uint32_t slot) const {
        return stagingGen_[slot & 1];
    }
    /// Slot of the most recent submission (-1 when none).
    [[nodiscard]] int lastSubmittedSlot() const {
        return submitted_ ? int((submitted_ - 1) % 2) : -1;
    }
    /// The consumer signals that its GPU reads of `slot` are queued
    /// (completion is queue-ordered — the next render on this slot is
    /// submitted later on the same queue).
    void shmRelease(uint32_t slot) {
        shmReleased_[slot & 1].fetch_add(1, std::memory_order_release);
        shmCv_.notify_all();
    }
    /// Bytes per staging-buffer row — 256-aligned (wgpu's
    /// COPY_BYTES_PER_ROW_ALIGNMENT for copy_buffer_to_texture).
    [[nodiscard]] vk::DeviceSize rowPitch() const {
        return (vk::DeviceSize(extent_.width) * 4 + 255)
               & ~vk::DeviceSize(255);
    }

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
    /// Set when a slot's submission never completed — the queue is
    /// wedged and further work is pointless. Frame entry points bail
    /// out and teardown leaks rather than touching in-flight objects.
    bool wedged_ = false;
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

    /// shm display path state — see setShmMode/shmRelease above.
    bool shmMode_ = false;
    /// How often each slot's staging buffer was re-created.
    std::array<uint32_t, 2> stagingGen_{};
    /// Renders submitted on each slot — a slot is reusable for render
    /// N+2 only once the consumer released render N on it.
    std::array<uint64_t, 2> shmRenders_{};
    /// Consumer releases per slot (see shmRelease).
    std::array<std::atomic<uint64_t>, 2> shmReleased_{};
    std::mutex shmMu_;
    std::condition_variable shmCv_;

    /// Blit state: eLoad-variant render pass + snapshot image.
    vk::UniqueRenderPass renderPassLoad_;
    core::Image blitImage_;
    bool blitCaptured_ = false;

    /// GPU service facade (lazily created by gpu()).
    std::unique_ptr<render::GpuServices> gpuServices_;
};

} // namespace volcano::backend
