// volcano/text/TextRenderer.hpp — glyb-based bitmap atlas text renderer
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/Image.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations for glyb types
struct font_manager_ft;
struct text_shaper_hb;
struct text_renderer_ft;
struct font_face;

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::text {

/// Renders text using glyb's FreeType + HarfBuzz bitmap atlas.
/// Glyphs are rasterized on-demand into a font atlas bitmap, uploaded
/// to a Vulkan texture, and rendered as textured quads. This correctly
/// handles glyph holes (o, 0, A, etc.) via FreeType's span rasterizer.
class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    void init(vk::Device device, VmaAllocator allocator,
              vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache,
              core::DescriptorPool& descPool);

    /// Pre-render common ASCII glyphs and upload the atlas texture.
    /// Must be called after init() and before any draw() calls.
    /// Uses a one-time command buffer (outside any render pass).
    void prepareAtlas(vk::Queue queue, vk::CommandPool pool);

    /// Reset per-frame scratch buffers. Call at the start of each frame.
    void resetScratch();

    /// Draw a UTF-8 string at (x, y) in pixel coords with the given color.
    /// (x, y) is the baseline position (top-left of the text block).
    /// `rotation` is in radians (clockwise in screen space, Y-down).
    /// The text is rotated around the (x, y) origin point.
    /// Multi-line strings (separated by '\n') are drawn with each line
    /// aligned within the block according to `lineAlign`.
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              std::string_view text, float x, float y,
              plot::Color color, float scale = 1.0f,
              float rotation = 0.0f,
              plot::HAlign lineAlign = plot::HAlign::Left);

    /// Measure the bounding box of a UTF-8 string at the given scale.
    /// Returns {width, height, ascent} in pixels.
    /// width = total horizontal advance, height = ascent + descent,
    /// ascent = distance from baseline to top of text.
    /// Multi-line strings: width = widest line, height covers all lines,
    /// ascent = first line's ascent.
    struct TextMetrics { float width; float height; float ascent; };
    TextMetrics measureText(std::string_view text, float scale = 1.0f);

    /// Font line advance (ascent+descent+leading) in pixels at `scale`.
    float lineHeight(float scale = 1.0f);

    /// True when draw() rasterized new glyphs into the CPU-side atlas
    /// since the last GPU upload (e.g. first CJK/extended characters).
    /// Call syncAtlas() outside a render pass, then re-render the frame.
    [[nodiscard]] bool atlasDirty() const noexcept { return atlasDirty_; }
    /// Re-upload the atlas texture to the GPU (outside any render pass).
    void syncAtlas(vk::Queue queue, vk::CommandPool pool);

private:
    vk::Device device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    vk::UniqueDescriptorSetLayout descSetLayout_;
    vk::UniqueDescriptorPool descPool_;
    vk::DescriptorSet descSet_;
    vk::UniqueSampler sampler_;
    bool inited_ = false;

    // glyb font manager, shaper, and renderer
    std::unique_ptr<font_manager_ft> fontManager_;
    std::unique_ptr<text_shaper_hb> shaper_;
    std::unique_ptr<text_renderer_ft> textRenderer_;
    font_face* fontFace_ = nullptr;
    /// Broad-coverage fallback face (CJK/RTL/…); glyphs missing from
    /// fontFace_ shape against this instead (shared atlas).
    font_face* fallbackFace_ = nullptr;

    // Atlas texture (uploaded lazily, re-uploaded when it grows)
    core::Image atlasImage_;
    vk::UniqueImageView atlasView_;
    bool atlasUploaded_ = false;
    bool atlasDirty_ = false;
    size_t atlasGlyphCount_ = 0;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;

    // Scratch buffers for vertices and indices (ring-buffered per frame)
    core::Buffer scratchVB_;
    core::Buffer scratchIB_;
    size_t vbCapacity_ = 0;
    size_t ibCapacity_ = 0;
    size_t vbOffset_ = 0;   // current write offset within scratchVB_
    size_t ibOffset_ = 0;   // current write offset within scratchIB_

    // glyb draw list (reused per draw call, allocated in init)
    // Stored as void* to avoid pulling glyb headers into this header.
    void* batch_ = nullptr;

    /// Ensure scratch buffers can hold the given vertex/index counts.
    void ensureScratch(size_t vertexBytes, size_t indexBytes);

    /// Find and load a system font.
    void loadFont();
    /// Pre-render the ASCII + math-symbol charset into the CPU atlas.
    void prepareAtlasGlyphs();
    /// Upload the current atlas bitmap to the GPU. Called by
    /// prepareAtlas (first upload) and syncAtlas (growth re-upload).
    void uploadAtlas(vk::Queue queue, vk::CommandPool pool);
};

} // namespace volcano::text
