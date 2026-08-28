// volcano/text/Font.cpp
#include "volcano/text/Font.hpp"

#include <stdexcept>

// FreeType is optional; if not available, text rendering is disabled.
#ifdef VOLCANO_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace volcano::text {

namespace {

#ifdef VOLCANO_HAS_FREETYPE
FT_Library g_ft = nullptr;

FT_Library ftLibrary() {
    if (!g_ft) FT_Init_FreeType(&g_ft);
    return g_ft;
}
#endif

} // namespace

Font::Font(const std::filesystem::path& path, float size) : size_(size) {
#ifdef VOLCANO_HAS_FREETYPE
    auto lib = ftLibrary();
    FT_Face face;
    if (FT_New_Face(lib, path.string().c_str(), 0, &face)) {
        valid_ = false; return;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size));
    face_ = face;
    valid_ = true;
#else
    (void)path;
    valid_ = false;
#endif
}

Font::~Font() {
#ifdef VOLCANO_HAS_FREETYPE
    if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
#endif
}

Font::Font(Font&& o) noexcept : face_(o.face_), size_(o.size_), valid_(o.valid_) {
    o.face_ = nullptr; o.valid_ = false;
}

Font& Font::operator=(Font&& o) noexcept {
    if (this != &o) {
#ifdef VOLCANO_HAS_FREETYPE
        if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
#endif
        face_ = o.face_; size_ = o.size_; valid_ = o.valid_;
        o.face_ = nullptr; o.valid_ = false;
    }
    return *this;
}

void Font::rasterizeAtlas(std::u32string_view /*codepoints*/,
                          std::vector<uint8_t>& /*atlasBits*/,
                          uint32_t& /*atlasW*/, uint32_t& /*atlasH*/,
                          std::vector<GlyphInfo>& /*glyphs*/) {
#ifdef VOLCANO_HAS_FREETYPE
    // TODO: rasterize each glyph, pack into atlas, populate glyph metadata.
#else
    throw std::runtime_error("FreeType not available; text rendering disabled");
#endif
}

std::filesystem::path findSystemFont(std::string_view family) {
    // Common Linux font directories.
    std::vector<std::filesystem::path> dirs = {
        "/usr/share/fonts", "/usr/local/share/fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".local/share/fonts",
    };
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            if (name.find(family) != std::string::npos) return e.path();
        }
    }
    return {};
}

} // namespace volcano::text
