// src/render/VulkanGpuServices.hpp — Vulkan implementation of GpuServices
//
// NATIVE BUILDS ONLY (never compiled into the WASM target). Owns the
// shared primitive renderers, the pipeline cache/descriptor pool, the
// service buffer registry, and the bespoke compute paths relocated from
// the plot classes (histBin / violinNormalize / pcmTessellate).
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/core/Buffer.hpp>
#include <volcano/core/CommandBuffer.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/render/GpuServices.hpp>
#include <volcano/render/VkCmd.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <functional>
#include <unordered_map>

namespace volcano::render {

class VulkanGpuServices : public GpuServices {
public:
    /// `ctx`/`renderPass`/`formats`/`samples` come from the backend.
    /// The services are created lazily by IBackend::gpu() once the
    /// backend's targets exist.
    VulkanGpuServices(backend::GpuContext& ctx, vk::RenderPass renderPass,
                      vk::Format colorFormat, vk::Format depthFormat,
                      vk::SampleCountFlagBits samples);
    ~VulkanGpuServices() override;

    // ── Native accessors (impl ctors/factories + internal draws) ─────
    [[nodiscard]] vk::Device device() const { return ctx_.device.handle(); }
    [[nodiscard]] VmaAllocator allocator() const {
        return ctx_.allocator.handle();
    }
    [[nodiscard]] vk::Queue graphicsQueue() const {
        return ctx_.device.graphicsQueue();
    }
    [[nodiscard]] vk::CommandPool graphicsPool() const {
        return ctx_.graphicsPool.handle();
    }
    [[nodiscard]] vk::Queue computeQueue() const {
        return ctx_.device.computeQueue();
    }
    [[nodiscard]] vk::CommandPool computePool() const {
        return ctx_.computePool.handle();
    }
    [[nodiscard]] vk::RenderPass renderPass() const { return renderPass_; }
    [[nodiscard]] vk::Format colorFormat() const { return colorFormat_; }
    [[nodiscard]] vk::Format depthFormat() const { return depthFormat_; }
    [[nodiscard]] vk::SampleCountFlagBits samples() const { return samples_; }
    core::PipelineCache& pipelineCache();
    core::DescriptorPool& descPool();
    void setSamples(vk::SampleCountFlagBits s) { samples_ = s; }

    /// Resolve a service buffer token to its VkBuffer (0 → VK_NULL_HANDLE).
    [[nodiscard]] vk::Buffer vkBufferOf(GpuBuf token) const;
    /// Resolve to the owning core::Buffer (nullptr when unknown).
    [[nodiscard]] core::Buffer* bufferOf(GpuBuf token);
    /// Adopt ownership of an existing core::Buffer under a fresh token —
    /// used by compute paths that allocate their own outputs.
    GpuBuf adoptBuffer(core::Buffer&& buf);

    /// One-shot command recording + submit on the graphics queue —
    /// the primitive impls' upload path (mirrors core::OneTimeCommands).
    void oneTimeSubmit(std::function<void(vk::CommandBuffer)> fn);

    // ── GpuServices ──────────────────────────────────────────────────
    [[nodiscard]] plot::Extent2D extent() const override { return extent_; }
    void setExtent(plot::Extent2D e) { extent_ = e; }

    /// Idempotent lazy init of the shared renderers (parallel shader
    /// compile — mirrors the old Renderer::prepare() init block).
    void ensureGraphics();
    [[nodiscard]] bool graphicsReady() const noexcept { return graphicsReady_; }
    [[nodiscard]] bool textReady() const noexcept { return textReady_; }

    primitives::SpineRenderer& spine() override;
    primitives::PointRenderer& sharedPoints() override;
    primitives::InstancedPathRenderer& instancedPath() override;
    primitives::GpuLineRenderer& gpuLine() override;
    primitives::ReduceRenderer& reduce() override;
    primitives::KdeEvalRenderer& kdeEval() override;
    Grid3DRenderer& grid3D() override;
    text::TextRenderer& text() override;

    std::unique_ptr<primitives::PointRenderer> createPointRenderer() override;
    std::unique_ptr<primitives::LineRenderer> createLineRenderer() override;
    std::unique_ptr<primitives::LineSegmentRenderer>
        createLineSegmentRenderer() override;
    std::unique_ptr<primitives::FillRenderer> createFillRenderer() override;
    std::unique_ptr<primitives::BarRenderer> createBarRenderer() override;
    std::unique_ptr<primitives::PieRenderer> createPieRenderer() override;
    std::unique_ptr<primitives::HeatmapRenderer>
        createHeatmapRenderer() override;
    std::unique_ptr<primitives::SurfaceRenderer>
        createSurfaceRenderer() override;
    std::unique_ptr<primitives::InstancedPathRenderer>
        createInstancedPathRenderer() override;
    std::unique_ptr<primitives::EvalRenderer> createEvalRenderer() override;

    GpuBuf createBuffer(const GpuBufferDesc& desc) override;
    void writeBuffer(GpuBuf buf, uint64_t offset,
                     std::span<const std::byte> data) override;
    void destroyBuffer(GpuBuf buf) override;

    void beginFrameScratch() override;
    std::unique_ptr<Cmd> beginPrePass() override;
    void submitPrePass(std::unique_ptr<Cmd> cmd) override;

    void ensureText() override;
    void syncTextAtlas() override;

    std::optional<std::vector<uint32_t>> histBin(
        std::span<const float> data, uint32_t nBins,
        float lo, float hi) override;
    std::optional<std::vector<float>> violinNormalize(
        std::span<const float> src, uint32_t n) override;
    bool pcmTessellate(GpuBuf x, GpuBuf y, GpuBuf t, GpuBuf lut,
                       uint32_t lutSize, GpuBuf posOut, GpuBuf colOut,
                       uint32_t cells) override;

private:
    backend::GpuContext& ctx_;
    vk::RenderPass renderPass_;
    vk::Format colorFormat_;
    vk::Format depthFormat_;
    vk::SampleCountFlagBits samples_;
    plot::Extent2D extent_{};

    std::unique_ptr<core::PipelineCache> pipelineCache_;
    std::unique_ptr<core::DescriptorPool> descPool_;

    // Shared renderers (lazy — created by ensureGraphics()/ensureText())
    std::unique_ptr<primitives::SpineRenderer> spine_;
    std::unique_ptr<primitives::PointRenderer> points_;
    std::unique_ptr<primitives::InstancedPathRenderer> instancedPath_;
    std::unique_ptr<primitives::GpuLineRenderer> gpuLine_;
    std::unique_ptr<primitives::ReduceRenderer> reduce_;
    std::unique_ptr<primitives::KdeEvalRenderer> kdeEval_;
    std::unique_ptr<Grid3DRenderer> grid3D_;
    std::unique_ptr<text::TextRenderer> text_;
    bool graphicsReady_ = false;
    bool textInited_ = false;
    bool textReady_ = false;

    /// Service-owned buffer registry (createBuffer/adoptBuffer).
    std::unordered_map<GpuBuf, core::Buffer> bufs_;
    GpuBuf nextBufToken_ = 1;

    /// Pre-pass command buffer for IPlot::preDraw compute work.
    std::optional<core::CommandBuffer> preCmd_;
};

} // namespace volcano::render
