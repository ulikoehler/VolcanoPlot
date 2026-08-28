// volcano/text/Font.hpp — font abstraction (FreeType / system font)
#pragma once

#include <volcano/plot/Types.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace volcano::text {

struct GlyphInfo {
    uint32_t codepoint = 0;
    uint32_t atlasX = 0;     // position in atlas
    uint32_t atlasY = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t bearingX = 0;
    int32_t bearingY = 0;
    int32_t advance = 0;
};

class Font {
public:
    Font() = default;
    /// Load a font from a file path (TTF/OTF).
    explicit Font(const std::filesystem::path& path, float size = 12.0f);
    ~Font();

    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    [[nodiscard]] float size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    /// Rasterize a set of glyphs into a grayscale bitmap atlas.
    /// Returns the atlas bitmap (width*height bytes) and glyph metadata.
    void rasterizeAtlas(std::u32string_view codepoints,
                        std::vector<uint8_t>& atlasBits,
                        uint32_t& atlasW, uint32_t& atlasH,
                        std::vector<GlyphInfo>& glyphs);

private:
    void* face_ = nullptr; // FT_Face, opaque
    float size_ = 12.0f;
    bool valid_ = false;
};

/// Find a system font by family name.
std::filesystem::path findSystemFont(std::string_view family);

} // namespace volcano::text
