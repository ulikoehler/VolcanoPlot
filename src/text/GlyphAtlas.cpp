// volcano/text/GlyphAtlas.cpp
#include "volcano/text/GlyphAtlas.hpp"

namespace volcano::text {

void GlyphAtlas::build(const Font& font, std::u32string_view codepoints) {
    std::vector<GlyphInfo> glyphList;
    font.outlineAtlas(codepoints, glyphList);
    glyphs_.clear();
    totalTriangles_ = 0;
    for (auto& g : glyphList) {
        totalTriangles_ += g.mesh.vertices.size() / 3;
        glyphs_[g.codepoint] = std::move(g);
    }
}

const GlyphInfo* GlyphAtlas::glyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return it == glyphs_.end() ? nullptr : &it->second;
}

} // namespace volcano::text
