// volcano/render/primitives/InstancedPathRenderer.hpp — instanced path fill
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <span>
#include <vector>

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::render::primitives {

/// Per-instance attributes for instanced path rendering.
struct PathInstance {
    float ox, oy;       ///< instance origin, pixel space (Y-down)
    float sx, sy;       ///< template→pixel scale (sy may be negative)
    float r, g, b, a;   ///< fill color
};

/// Draws many copies of a template path (triangle soup) at per-instance
/// offsets/scales/colors in a single instanced draw call. Used by
/// PathCollection to keep large collections (the machinery behind
/// scatter) off the per-item CPU tessellation path.
class InstancedPathRenderer {
public:
    InstancedPathRenderer() = default;
    void init(vk::Device device, VmaAllocator allocator,
              vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples,
              core::PipelineCache& cache,
              core::DescriptorPool& descPool);

    [[nodiscard]] bool inited() const noexcept { return inited_; }
    [[nodiscard]] uint32_t templateVertCount() const noexcept {
        return templateVerts_;
    }

    /// Upload the template triangle soup (template space, unit-ish).
    void setTemplate(vk::Device device, vk::Queue queue,
                     vk::CommandPool pool,
                     std::span<const plot::Point2D> triVerts);

    /// One instanced draw of the template at each instance transform.
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent.
    void drawInstanced(vk::CommandBuffer cmd, vk::Rect2D clip,
                       vk::Extent2D resolution,
                       std::span<const PathInstance> instances);

    /// Reset the scratch instance buffer offset (call once per frame).
    /// Also releases buffers retired by mid-frame growth.
    void resetScratch() {
        scratchOffset_ = 0;
        retiredScratch_.clear();
    }

private:
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    bool inited_ = false;

    core::Buffer templateVB_;
    uint32_t templateVerts_ = 0;

    core::Buffer scratchVB_;
    std::vector<core::Buffer> retiredScratch_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;

    void ensureScratch(size_t byteCount);
};

} // namespace volcano::render::primitives
