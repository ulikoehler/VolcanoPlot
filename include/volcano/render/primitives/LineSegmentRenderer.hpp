// volcano/render/primitives/LineSegmentRenderer.hpp — disconnected line segments
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/Transform.hpp>
#include <vulkan/vulkan.hpp>

namespace volcano::core { class PipelineCache; }

namespace volcano::render::primitives {

/// Renders disconnected line segments using `eLineList` topology.
/// Each pair of vertices forms one line segment. Used for error bars,
/// caps, vlines/hlines, reference lines, etc.
///
/// Color and width are uniform (push constants), same as LineRenderer.
class LineSegmentRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);

    /// Upload line segment endpoints. Each consecutive pair of points
    /// forms one line segment. `points` must have an even number of elements.
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, std::span<const plot::Point2D> points,
                plot::Color color, float width);

    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Transform2D& transform, uint32_t vertexCount) const;

    /// GPU handle to the uploaded position buffer (for GPU autoscale).
    [[nodiscard]] vk::Buffer pointBuffer() const { return pointBuffer_.handle(); }
    [[nodiscard]] uint32_t pointCount() const { return count_; }

private:
    vk::Device device_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer pointBuffer_;
    plot::Color color_;
    float width_ = 1.0f;
    uint32_t count_ = 0;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
