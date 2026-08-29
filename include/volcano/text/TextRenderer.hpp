// volcano/text/TextRenderer.hpp — vectorized text renderer
#pragma once

#include "volcano/text/GlyphAtlas.hpp"
#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <memory>

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::text {

/// Renders text as filled triangle meshes (vectorized glyphs).
/// Each draw call builds a vertex buffer of all glyph triangles for the
/// string, offset to the correct screen position, and draws them.
class TextRenderer {
public:
    TextRenderer() = default;
    void init(vk::Device device, VmaAllocator allocator,
              vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache,
              core::DescriptorPool& descPool);
    /// Set the glyph atlas to use.
    void setAtlas(std::shared_ptr<GlyphAtlas> atlas);
    /// Reset the scratch vertex buffer offset. Call at the start of each frame.
    void resetScratch() { scratchOffset_ = 0; }
    /// Draw a UTF-8 string at (x, y) in pixel coords with the given color.
    /// (x, y) is the baseline position (bottom-left of the first glyph).
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              std::string_view text, float x, float y,
              plot::Color color, float scale = 1.0f);

private:
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    std::shared_ptr<GlyphAtlas> atlas_;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    bool inited_ = false;

    /// Persistently mapped host-visible scratch vertex buffer.
    /// Uses a ring-buffer scheme: each draw call advances the offset,
    /// so multiple draws in the same frame don't overwrite each other.
    core::Buffer scratchVB_;
    size_t scratchCapacity_ = 0;
    size_t scratchOffset_ = 0;  // current write offset in bytes

    /// Ensure scratchVB_ can hold at least byteCount bytes at the current offset.
    /// If not enough space, the buffer is grown and the offset reset to 0.
    void ensureScratch(size_t byteCount);

    /// Convert UTF-8 to UTF-32.
    static std::u32string utf8ToUtf32(std::string_view utf8);
};

} // namespace volcano::text
