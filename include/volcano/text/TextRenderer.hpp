// volcano/text/TextRenderer.hpp — glyb-based bitmap atlas text renderer
//
// Base class holding the CPU side (FreeType rasterization, HarfBuzz
// shaping, glyph atlas bitmap, measurement). GPU transport is backend
// virtual: TextRendererVk uploads the atlas to a Vulkan texture and
// draws textured quads; the WebGPU impl records WriteTexture +
// DrawTextQuads ops into the stream.
// Instances come from GpuServices::text() — never constructed directly.
#pragma once

#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward declarations for glyb types
struct font_manager_ft;
struct text_shaper_hb;
struct text_renderer_ft;
struct font_face;

namespace volcano::text {

/// Metadata read from a font file via FreeType (family/style names
/// from the name table plus bold/italic style flags). familyName is
/// empty when the file can't be opened as a font.
struct FontFileInfo {
    std::string familyName;
    std::string styleName;
    bool bold = false;
    bool italic = false;
};

/// Open `path` with FreeType and read its name-table metadata.
/// Used by the Python font_manager bindings to populate FontEntry
/// names (mpl reads them via ft2font the same way).
FontFileInfo fontFileInfo(const std::string& path);

/// Renders text using glyb's FreeType + HarfBuzz bitmap atlas.
/// Glyphs are rasterized on-demand into a font atlas bitmap; the GPU
/// implementation uploads it and draws textured quads. This correctly
/// handles glyph holes (o, 0, A, etc.) via FreeType's span rasterizer.
class TextRenderer {
public:
    TextRenderer();
    virtual ~TextRenderer();

    /// CPU-only instance (font metrics/measuring; draw()/atlas are
    /// no-ops). Used by the Python binding's text measurer.
    static std::unique_ptr<TextRenderer> createCpuOnly();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// CPU-only font init — loads faces/shaper so measureText()/
    /// faceFor() work without any GPU (mathtext metrics, WASM).
    void initFonts();

    /// Upload the atlas / finish GPU-side init.
    /// Vulkan: one-time command submission outside any render pass.
    /// Op backend: emits WriteTexture ops into the current stream.
    virtual void prepareAtlasGpu() = 0;

    /// Reset per-frame scratch buffers. Call at the start of each frame.
    virtual void resetScratch() = 0;

    /// Draw a UTF-8 string at (x, y) in pixel coords with the given color.
    /// (x, y) is the baseline position (top-left of the text block).
    /// `rotation` is in radians (clockwise in screen space, Y-down).
    /// The text is rotated around the (x, y) origin point.
    /// Multi-line strings (separated by '\n') are drawn with each line
    /// aligned within the block according to `lineAlign`.
    /// `face` selects a non-default face (e.g. serif for dejavuserif
    /// mathtext); nullptr uses the primary face.
    virtual void draw(render::Cmd& cmd, plot::Rect2D rect,
                      std::string_view text, float x, float y,
                      plot::Color color, float scale = 1.0f,
                      float rotation = 0.0f,
                      plot::HAlign lineAlign = plot::HAlign::Left,
                      font_face* face = nullptr) = 0;

    /// Measure the bounding box of a UTF-8 string at the given scale.
    /// Returns {width, height, ascent} in pixels.
    /// width = total horizontal advance, height = ascent + descent,
    /// ascent = distance from baseline to top of text.
    /// Multi-line strings: width = widest line, height covers all lines,
    /// ascent = first line's ascent.
    struct TextMetrics { float width; float height; float ascent; };
    TextMetrics measureText(std::string_view text, float scale = 1.0f,
                            font_face* face = nullptr);

    /// Serif face for the dejavuserif mathtext fontset (nullptr when the
    /// system has no DejaVu Serif). Shares the primary face's atlas.
    [[nodiscard]] font_face* serifFace() const noexcept { return serifFace_; }

    /// mpl fontproperties resolution: pick a face matching
    /// family/style/weight, lazily loading matching system fonts (they
    /// share the primary atlas). Returns the best match — the primary
    /// face when nothing better is found. `isBold`/`isItalic` reflect
    /// whether the returned face genuinely provides that styling so
    /// callers can fall back to faux-bold when needed.
    struct FaceMatch { font_face* face; bool bold; bool italic; };
    [[nodiscard]] FaceMatch faceFor(std::string_view family,
                                    std::string_view style,
                                    std::string_view weight);

    /// Font line advance (ascent+descent+leading) in pixels at `scale`.
    float lineHeight(float scale = 1.0f);

    /// True when draw() rasterized new glyphs into the CPU-side atlas
    /// since the last GPU upload (e.g. first CJK/extended characters).
    /// Call syncAtlas() outside a render pass, then re-render the frame.
    [[nodiscard]] bool atlasDirty() const noexcept { return atlasDirty_; }
    /// Re-upload the atlas texture (outside any render pass).
    virtual void syncAtlas() = 0;

protected:
    // glyb font manager, shaper, and renderer (CPU side)
    std::unique_ptr<font_manager_ft> fontManager_;
    std::unique_ptr<text_shaper_hb> shaper_;
    std::unique_ptr<text_renderer_ft> textRenderer_;
    font_face* fontFace_ = nullptr;
    /// Broad-coverage fallback face (CJK/RTL/…); glyphs missing from
    /// fontFace_ shape against this instead (shared atlas).
    font_face* fallbackFace_ = nullptr;
    /// DejaVu Serif — the dejavuserif mathtext fontset face.
    font_face* serifFace_ = nullptr;
    /// Lazily-resolved faces for fontproperties (family/style/weight).
    std::unordered_map<std::string, FaceMatch> faceCache_;

    /// Set by impls when draw() rasterizes previously-unseen glyphs into
    /// the CPU atlas bitmap (needs a re-upload).
    bool atlasDirty_ = false;
    size_t shapedGlyphs_ = 0;  // glyph_map size at last shapeText()
    /// Atlas bitmap dimensions (CPU side); impls upload this to GPU.
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;

    // glyb draw list (reused per draw call, allocated in init)
    // Stored as void* to avoid pulling glyb headers into this header.
    void* batch_ = nullptr;

    /// Find and load a system font.
    void loadFont();
    /// Pre-render the ASCII + math-symbol charset into the CPU atlas.
    void prepareAtlasGlyphs();

    /// glyb draw_vertex POD mirror: pos[3], uv[2], color(u32 RGBA8),
    /// shape(f32). Struct layout must match glyb's draw_vertex (28 B).
    struct GlyphVert {
        float px, py, pz, u, v;
        uint32_t color;
        float shape;
    };
    /// Shape `text` into the shared draw list (positions in pixel space
    /// at reference size, pre-scale/rotation; (x,y) is the block origin).
    /// Clears the batch first. Returns false when nothing was emitted.
    /// Sets atlasDirty_ when new glyphs were rasterized.
    bool shapeText(std::string_view text, float x, float y,
                   uint32_t rgba8, plot::HAlign lineAlign,
                   font_face* face);
    /// Shaped vertices/indices of the last shapeText() call.
    std::span<const GlyphVert> batchVertices() const;
    std::span<const uint32_t> batchIndices() const;
    /// Current atlas bitmap (nullptr until a font loaded). `depth` is
    /// bytes per pixel (1 = gray, 4 = msdf/color).
    const void* atlasPixels(int& w, int& h, int& depth) const;
    size_t glyphCount() const;
};

} // namespace volcano::text
