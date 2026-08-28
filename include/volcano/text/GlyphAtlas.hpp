// volcano/text/GlyphAtlas.hpp — SDF glyph atlas
#pragma once

#include "volcano/text/Font.hpp"
#include <volcano/core/Image.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace volcano::text {

class GlyphAtlas {
public:
    GlyphAtlas() = default;
    /// Build the atlas from a font + set of codepoints.
    void build(vk::Device device, vk::Queue queue, vk::CommandPool pool,
               VmaAllocator allocator, const Font& font,
               std::u32string_view codepoints);
    [[nodiscard]] const GlyphInfo* glyph(uint32_t codepoint) const;
    [[nodiscard]] vk::ImageView view() const noexcept;
    [[nodiscard]] uint32_t width() const noexcept { return atlasW_; }
    [[nodiscard]] uint32_t height() const noexcept { return atlasH_; }

private:
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    core::Image atlasImage_;
    vk::UniqueImageView atlasView_;
    uint32_t atlasW_ = 0, atlasH_ = 0;
};

} // namespace volcano::text
