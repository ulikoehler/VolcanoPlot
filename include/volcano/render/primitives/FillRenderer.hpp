// volcano/render/primitives/FillRenderer.hpp — filled polygon renderer
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/Transform.hpp>
#include <vulkan/vulkan.hpp>

namespace volcano::core { class PipelineCache; }

namespace volcano::render::primitives {

/// Renders filled polygons (triangle lists) in data-space coordinates.
/// Used by FillPlot (filled polygon) and FillBetweenPlot (fill between
/// two curves or a curve and a baseline).
///
/// The renderer accepts pre-built triangle vertex lists. Each vertex has
/// a position (Point2D, data coords) and a color (Color, RGBA float).
/// The vertex shader maps data coords to NDC using the viewport transform
/// (same as BarRenderer).
class FillRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);

    /// Upload triangle vertices. The caller builds the triangle list
    /// (e.g., via a triangle-strip-to-list or ear-clipping tessellation).
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator,
                std::span<const plot::Point2D> positions,
                std::span<const plot::Color> colors);

    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Transform2D& transform) const;

    /// GPU handle to the uploaded position buffer (for GPU autoscale).
    [[nodiscard]] vk::Buffer pointBuffer() const { return posBuffer_.handle(); }
    [[nodiscard]] uint32_t pointCount() const { return vertexCount_; }

private:
    vk::Device device_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer posBuffer_;
    core::Buffer colorBuffer_;
    uint32_t vertexCount_ = 0;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
