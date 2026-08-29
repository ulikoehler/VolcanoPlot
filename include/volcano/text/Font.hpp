// volcano/text/Font.hpp — font abstraction (FreeType / system font)
#pragma once

#include <volcano/plot/Types.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace volcano::text {

/// A 2D point in font units (26.6 fixed-point converted to float pixels).
struct FontPoint { float x; float y; };

/// Triangulated glyph mesh: a list of triangles (3 vertices each) in font-space.
/// Font-space: origin at glyph baseline, Y-up, units in pixels at the font's
/// rasterized size. The mesh covers the glyph's filled area.
struct GlyphMesh {
    std::vector<FontPoint> vertices;  // 3N vertices = N triangles
    float minX = 0, minY = 0, maxX = 0, maxY = 0;  // bounding box
};

struct GlyphInfo {
    uint32_t codepoint = 0;
    int32_t bearingX = 0;    // left bearing in pixels
    int32_t bearingY = 0;    // top bearing in pixels (Y-up: distance from baseline to top)
    int32_t advance = 0;     // advance width in pixels
    GlyphMesh mesh;          // triangulated outline
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

    /// Decompose a glyph outline into a triangulated mesh.
    /// Returns false if the glyph has no outline (e.g. space).
    [[nodiscard]] GlyphInfo outlineGlyph(uint32_t codepoint) const;

    /// Decompose multiple glyphs and return their metadata + meshes.
    void outlineAtlas(std::u32string_view codepoints,
                      std::vector<GlyphInfo>& glyphs) const;

private:
    void* face_ = nullptr; // FT_Face, opaque
    float size_ = 12.0f;
    bool valid_ = false;
};

/// Find a system font by family name.
std::filesystem::path findSystemFont(std::string_view family);

} // namespace volcano::text
