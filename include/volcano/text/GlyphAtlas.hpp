// volcano/text/GlyphAtlas.hpp — vectorized glyph atlas (triangulated outlines)
#pragma once

#include "volcano/text/Font.hpp"
#include <unordered_map>
#include <memory>

namespace volcano::text {

/// Stores triangulated glyph meshes for a set of codepoints.
/// No GPU texture — glyphs are rendered as filled triangle meshes.
class GlyphAtlas {
public:
    GlyphAtlas() = default;

    /// Build the atlas from a font + set of codepoints.
    /// Decomposes outlines, flattens beziers, and triangulates each glyph.
    void build(const Font& font, std::u32string_view codepoints);

    [[nodiscard]] const GlyphInfo* glyph(uint32_t codepoint) const;

    /// Total triangle count across all glyphs (for buffer sizing).
    [[nodiscard]] size_t totalTriangles() const noexcept { return totalTriangles_; }

private:
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    size_t totalTriangles_ = 0;
};

} // namespace volcano::text
