// volcano/text/TextRenderer.hpp — SDF text renderer
#pragma once

#include "volcano/text/GlyphAtlas.hpp"
#include <volcano/core/Buffer.hpp>
#include <volcano/plot/Types.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::text {

class TextRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache,
              core::DescriptorPool& descPool);
    /// Set the glyph atlas to use.
    void setAtlas(std::shared_ptr<GlyphAtlas> atlas);
    /// Draw a UTF-8 string at (x, y) in pixel coords with the given color.
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              std::string_view text, float x, float y,
              plot::Color color, float scale = 1.0f);

private:
    vk::Device device_;
    std::shared_ptr<GlyphAtlas> atlas_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    vk::UniqueDescriptorSet descSet_;
    vk::UniqueSampler sampler_;
    bool inited_ = false;
};

} // namespace volcano::text
