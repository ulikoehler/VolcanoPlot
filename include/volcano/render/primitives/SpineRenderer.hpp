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
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent
    /// used for the pixel→NDC transform (canvas pixels, not clip-relative).
    void drawRect(vk::CommandBuffer cmd, vk::Rect2D clip,
                  vk::Extent2D resolution,
                  plot::Rect2D rect, plot::Color color, float lineWidth);

    /// Draw a filled rectangle (two triangles).
    void drawFilledRect(vk::CommandBuffer cmd, vk::Rect2D clip,
                        vk::Extent2D resolution,
                        plot::Rect2D rect, plot::Color color);

    /// Reset the scratch vertex buffer offset. Call at the start of each
    /// frame. Also releases scratch buffers retired by mid-frame growth.
    void resetScratch() {
        scratchOffset_ = 0;
        retiredScratch_.clear();
    }

    /// Draw tick marks along an axis.
    /// yAxis=false: x-axis ticks at the bottom edge (down); true: y-axis
    /// ticks at the left edge (left).
    /// inFrac: fraction of tickLength pointing inside the axes
    /// (0 = "out", 1 = "in", 0.5 = "inout").
    /// farSide: x ticks at the top edge / y ticks at the right edge
    /// (matplotlib xaxis.top / yaxis.right).
    /// tickWidth: line width in pixels.
    void drawTicks(vk::CommandBuffer cmd, vk::Rect2D clip,
                   vk::Extent2D resolution,
                   plot::Rect2D rect, std::span<const float> positions,
                   plot::Color color, float tickLength,
                   bool yAxis, float dataMin, float dataMax,
                   float inFrac = 0.0f, bool farSide = false,
                   float tickWidth = 2.0f);

    /// Draw a line strip from the given pixel-space points.
    /// Used by reference line plots (AxhLine, AxvLine) that need to draw
    /// lines spanning the axes in pixel coordinates.
    void drawLineStrip(vk::CommandBuffer cmd, vk::Rect2D clip,
                       vk::Extent2D resolution,
                       std::span<const plot::Point2D> points,
                       plot::Color color, float width);

    /// Draw a pixel-space triangle soup (non-indexed: 3 vertices per tri).
    /// `clip` is the scissor rect; `resolution` is the framebuffer extent
    /// used for the pixel→NDC transform.
    /// Used by the polyline stroker for wide/dashed lines with joins & caps.
    void drawTriangles(vk::CommandBuffer cmd, vk::Rect2D clip,
                       vk::Extent2D resolution,
                       std::span<const plot::Point2D> triVerts,
                       plot::Color color);

    /// Draw a GPU-generated LineVertex triangle soup produced by
    /// GpuLineRenderer (compute-stroked polylines). `byteOffset` selects
    /// the first vertex inside `buffer`.
    void drawTrianglesGpu(vk::CommandBuffer cmd, vk::Rect2D clip,
                          vk::Extent2D resolution,
                          vk::Buffer buffer, vk::DeviceSize byteOffset,
                          uint32_t vertexCount);

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
    /// Scratch buffers retired by ensureScratch growth this frame; kept
    /// alive because recorded draw commands still reference them.
    std::vector<core::Buffer> retiredScratch_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;

    void ensureScratch(size_t byteCount);
};

} // namespace volcano::render::primitives
