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

private:
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer pointBuffer_;
    plot::Color color_;
    float width_ = 1.0f;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
