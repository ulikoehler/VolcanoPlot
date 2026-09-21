// volcano/render/primitives/LineRenderer.hpp — MSAA line strip renderer
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <volcano/plot/Types.hpp>

#include <vulkan/vulkan.hpp>

namespace volcano::core { class DescriptorPool; class PipelineCache; }

namespace volcano::render::primitives {

class LineRenderer {
public:
    LineRenderer() = default;
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, std::span<const plot::Point2D> points,
                plot::Color color, float width);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Transform2D& transform, uint32_t pointCount) const;

    /// GPU handle to the active point buffer (vec2 data) — the external
    /// buffer when bound, else the uploaded one. For GPU autoscale.
    [[nodiscard]] vk::Buffer pointBuffer() const noexcept {
        return externalBuf_ ? externalBuf_ : pointBuffer_.handle();
    }
    /// Number of uploaded points (0 until upload() is called).
    [[nodiscard]] uint32_t pointCount() const noexcept { return count_; }
    /// In-place data update: memcpy into the host-visible buffer when the
    /// new point count fits the existing allocation, else reallocate.
    /// The point buffer is allocated host-visible so this is a plain copy
    /// — no staging round-trip — the standard dynamic-VBO pattern.
    void updatePoints(std::span<const plot::Point2D> points);

    /// Draw from an externally-owned vec2 vertex buffer (e.g. a compute
    /// shader's output) instead of the uploaded buffer — GPU-resident
    /// data never round-trips through the host.
    void bindExternalBuffer(vk::Buffer buf, uint32_t count) {
        externalBuf_ = buf;
        count_ = count;
    }

private:
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer pointBuffer_;
    vk::Buffer externalBuf_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    plot::Color color_;
    float width_ = 1.0f;
    uint32_t count_ = 0;
    uint32_t capacity_ = 0;      ///< allocated point capacity
    bool inited_ = false;
};

} // namespace volcano::render::primitives
