// volcano/render/primitives/SpineRenderer.hpp — axis spine/border renderer
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::render::primitives {

/// Draws axis spines (border lines) around the axes rect in pixel space.
/// Uses a simple line-strip pipeline with pixel→NDC vertex transform.
class SpineRenderer {
public:
    SpineRenderer() = default;
    void init(vk::Device device, VmaAllocator allocator,
              vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples,
              core::PipelineCache& cache,
              core::DescriptorPool& descPool);

    /// Draw a rectangle border around the given pixel rect.
    void drawRect(vk::CommandBuffer cmd, vk::Rect2D scissor,
                  plot::Rect2D rect, plot::Color color, float lineWidth);

    /// Draw a filled rectangle (two triangles).
    void drawFilledRect(vk::CommandBuffer cmd, vk::Rect2D scissor,
                        plot::Rect2D rect, plot::Color color);

    /// Reset the scratch vertex buffer offset. Call at the start of each frame.
    void resetScratch() { scratchOffset_ = 0; }

    /// Draw tick marks along an axis.
    /// orientation: 0 = x-axis (ticks point down), 1 = y-axis (ticks point left).
    void drawTicks(vk::CommandBuffer cmd, vk::Rect2D scissor,
                   plot::Rect2D rect, std::span<const float> positions,
                   plot::Color color, float tickLength,
                   bool yAxis, float dataMin, float dataMax);

    /// Draw a line strip from the given pixel-space points.
    /// Used by reference line plots (AxhLine, AxvLine) that need to draw
    /// lines spanning the axes in pixel coordinates.
    void drawLineStrip(vk::CommandBuffer cmd, vk::Rect2D scissor,
                       std::span<const plot::Point2D> points,
                       plot::Color color, float width);

private:
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;        // line strip pipeline
    vk::UniquePipeline fillPipeline_;    // triangle list pipeline (filled rects)
    bool inited_ = false;

    /// Scratch vertex buffer (host-visible, ring-buffered).
    core::Buffer scratchVB_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;

    void ensureScratch(size_t byteCount);
};

} // namespace volcano::render::primitives
