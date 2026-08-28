// volcano/render/primitives/BarRenderer.hpp — bar chart renderer (stub, expandable)
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <vulkan/vulkan.hpp>

namespace volcano::core { class PipelineCache; }

namespace volcano::render::primitives {

class BarRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, const plot::BarData& data);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Transform2D& transform) const;
private:
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer vertexBuffer_;
    uint32_t vertexCount_ = 0;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
