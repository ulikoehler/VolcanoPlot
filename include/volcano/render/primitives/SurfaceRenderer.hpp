// volcano/render/primitives/SurfaceRenderer.hpp — 3D surface plot renderer
#pragma once
#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
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
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer vertexBuffer_;
    core::Buffer indexBuffer_;
    uint32_t indexCount_ = 0;
    float valueMin_ = 0, valueMax_ = 1;
    plot::Range gridXRange_{0,1}, gridYRange_{0,1};
    bool inited_ = false;
};
} // namespace volcano::render::primitives
