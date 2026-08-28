// volcano/render/primitives/SurfaceRenderer.hpp — 3D surface plot renderer (stub)
#pragma once
#include <volcano/core/Buffer.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Transform.hpp>
#include <vulkan/vulkan.hpp>
namespace volcano::core { class PipelineCache; }
namespace volcano::render::primitives {
class SurfaceRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, const plot::Grid2D& grid);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect, const plot::Camera3D& camera) const;
private:
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer vertexBuffer_;
    core::Buffer indexBuffer_;
    uint32_t indexCount_ = 0;
    bool inited_ = false;
};
} // namespace volcano::render::primitives
