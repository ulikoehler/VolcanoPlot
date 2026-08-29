// volcano/render/primitives/HeatmapRenderer.hpp — heatmap/KDE renderer
#pragma once
#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/core/Image.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Colormap.hpp>
#include <vulkan/vulkan.hpp>
namespace volcano::core { class PipelineCache; class DescriptorPool; }
namespace volcano::render::primitives {
class HeatmapRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache,
              core::DescriptorPool& descPool);
    void upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                VmaAllocator allocator, const plot::Grid2D& grid,
                const plot::Colormap& cmap);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect, const plot::Transform2D& transform) const;
private:
    vk::Device device_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Image gridImage_;
    core::Image cmapImage_; // 1D colormap LUT
    vk::UniqueImageView gridView_;
    vk::UniqueImageView cmapView_;
    vk::UniqueSampler sampler_;
    vk::DescriptorSet descSet_;
    core::Buffer quadBuffer_;
    float valueMin_ = 0, valueMax_ = 1;
    plot::Range gridXRange_{0,1}, gridYRange_{0,1};
    bool inited_ = false;
};
} // namespace volcano::render::primitives
