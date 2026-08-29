// volcano/render/GridRenderer.hpp — fwidth-based dynamic grid (matplotlib-style)
#pragma once

#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/Transform.hpp>
#include <vulkan/vulkan.hpp>

namespace volcano::core { class PipelineCache; }

namespace volcano::render {

/// Renders an anti-aliased grid using screen-space derivatives (fwidth).
/// Grid lines never quantize under zoom — same technique as the WebGPU chirp plot.
class GridRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache);
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Transform2D& transform,
              const plot::AxisStyle& xAxis, const plot::AxisStyle& yAxis) const;

private:
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    bool inited_ = false;
};

} // namespace volcano::render
