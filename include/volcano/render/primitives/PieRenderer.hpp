// volcano/render/primitives/PieRenderer.hpp — pie/donut chart renderer
#pragma once
#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <vulkan/vulkan.hpp>
namespace volcano::core { class PipelineCache; }
namespace volcano::render::primitives {
class PieRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, const plot::PieData& data);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect) const;
private:
    vk::Device device_;
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
