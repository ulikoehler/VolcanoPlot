// volcano/render/primitives/PointRenderer.hpp — MSAA scatter point renderer
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>

#include <vulkan/vulkan.hpp>

#include <memory>

namespace volcano::core { class Device; class DescriptorPool; }
namespace volcano::render { struct RenderContext; }

namespace volcano::render::primitives {

/// Renders scatter points as instanced quads with per-marker SDF shading.
/// Supports circle, square, diamond, triangle, plus, x, star markers.
class PointRenderer {
public:
    PointRenderer() = default;
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::DescriptorPool& descPool,
              core::PipelineCache& cache);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, std::span<const plot::Point2D> points,
                std::span<const plot::Color> colors, std::span<const float> sizes);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect, const plot::Transform2D& transform,
              uint32_t pointCount) const;

    /// GPU handle to the uploaded point buffer (vec2 data), for GPU autoscale.
    [[nodiscard]] vk::Buffer pointBuffer() const noexcept { return pointBuffer_.handle(); }
    /// Number of uploaded points (0 until upload() is called).
    [[nodiscard]] uint32_t pointCount() const noexcept { return count_; }

private:
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer pointBuffer_;
    core::Buffer colorBuffer_;
    core::Buffer sizeBuffer_;
    vk::UniqueDescriptorSet descSet_;
    uint32_t count_ = 0;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
