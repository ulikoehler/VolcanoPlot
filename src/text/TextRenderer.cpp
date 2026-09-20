// volcano/text/TextRenderer.cpp — glyb-based bitmap atlas text renderer
#include "volcano/text/TextRenderer.hpp"
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

// glyb headers (must be included in dependency order — glyb headers
// don't include their own dependencies)
#include "binpack.h"
#include "utf8.h"
#include "image.h"
#include "draw.h"
#include "font.h"
#include "glyph.h"

namespace volcano::text {

namespace {

// glyb draw_vertex layout: pos[3], uv[2], color(u32), shape(f32) = 28 bytes
// But we use a simplified vertex for our Vulkan pipeline.
struct TextVertex {
    float x, y;        // pixel position (screen-space, Y-down)
    float u, v;        // atlas UV
    float r, g, b, a;  // color
};

constexpr const char* kVertGlsl = R"(
#version 460
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(push_constant) uniform PC {
    vec2 u_resolution;
} pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() {
    // Pixel coords (top-left origin, Y-down) → Vulkan NDC (Y-down).
    vec2 ndc = vec2(
        a_pos.x / pc.u_resolution.x * 2.0 - 1.0,
        a_pos.y / pc.u_resolution.y * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

constexpr const char* kFragGlsl = R"(
#version 460
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D u_atlas;
float median(vec3 v) {
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}
void main() {
    // MSDF atlas: signed distance is the median of the RGB channels.
    vec3 sd = texture(u_atlas, v_uv).rgb;
    float dist = median(sd) - 0.5;
    float w = fwidth(dist);
    float alpha = smoothstep(-w, w, dist);
    outColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

// Find a system font file (DejaVu Sans or similar).
std::string findSystemFontFile() {
    std::vector<std::filesystem::path> dirs = {
        "/usr/share/fonts", "/usr/local/share/fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".local/share/fonts",
    };
    // Filter out non-regular weights/styles.
    auto isRegular = [](const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.find("bold") == std::string::npos &&
               lower.find("italic") == std::string::npos &&
               lower.find("oblique") == std::string::npos &&
               lower.find("condensed") == std::string::npos &&
               lower.find("light") == std::string::npos &&
               lower.find("thin") == std::string::npos &&
               lower.find("black") == std::string::npos &&
               lower.find("heavy") == std::string::npos &&
               lower.find("medium") == std::string::npos &&
               lower.find("semibold") == std::string::npos &&
               lower.find("extralight") == std::string::npos &&
               lower.find("demi") == std::string::npos;
    };
    // First pass: look for exact "DejaVuSans.ttf" (the regular weight).
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            // Exact match on "DejaVuSans.ttf" (case-insensitive).
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "dejavusans.ttf")
                return e.path().string();
        }
    }
    // Second pass: any DejaVu Sans that passes the regular filter.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            if (name.find("DejaVuSans") != std::string::npos && isRegular(name))
                return e.path().string();
        }
    }
    // Third pass: any regular TTF/OTF.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            auto ext = e.path().extension().string();
            std::string extLower = ext;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
            if ((extLower == ".ttf" || extLower == ".otf") && isRegular(name))
                return e.path().string();
        }
    }
    return {};
}

/// Codepoints probed when ranking fallback fonts: Latin, Greek,
/// Cyrillic, Hebrew, Arabic, CJK ideograph, hiragana, hangul.
constexpr uint32_t kCoverageProbes[] = {
    0x0041, 0x03B2, 0x0416, 0x05D0, 0x0627, 0x4E2D, 0x3042, 0xD55C};

/// Number of probe codepoints covered by the font at `path` (opened
/// transiently — not registered with the font manager).
int probeFontCoverage(FT_Library ftlib, const std::filesystem::path& path) {
    FT_Face probe = nullptr;
    if (FT_New_Face(ftlib, path.string().c_str(), 0, &probe)) return 0;
    int score = 0;
    for (uint32_t cp : kCoverageProbes)
        if (FT_Get_Char_Index(probe, cp) != 0) ++score;
    FT_Done_Face(probe);
    return score;
}

/// Scan system font directories for the regular font with the broadest
/// probe coverage. Returns {} when nothing beats `primaryPath`.
std::string findFallbackFontFile(FT_Library ftlib,
                                 const std::string& primaryPath) {
    std::vector<std::filesystem::path> dirs = {
        "/usr/share/fonts", "/usr/local/share/fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".local/share/fonts",
    };
    int bestScore = probeFontCoverage(ftlib, primaryPath);
    std::string best;
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            auto path = e.path().string();
            if (path == primaryPath) continue;
            int score = probeFontCoverage(ftlib, e.path());
            if (score > bestScore) { bestScore = score; best = path; }
        }
    }
    return best;
}

/// Does `face` provide a glyph for `cp`?
bool faceCovers(font_face* face, uint32_t cp) {
    auto* ft = static_cast<font_face_ft*>(face);
    return ft && ft->ftface && FT_Get_Char_Index(ft->ftface, cp) != 0;
}

/// Pick the face covering `cp`: primary → fallback → primary (.notdef).
font_face* faceFor(font_face* primary, font_face* fallback, uint32_t cp) {
    if (faceCovers(primary, cp)) return primary;
    if (faceCovers(fallback, cp)) return fallback;
    return primary;
}

/// font_manager_ft subclass that shares ONE atlas across all faces —
/// fallback-face glyphs land in the same GPU texture (glyph entries are
/// keyed by font_id). Also fixes upstream's uninitialized defaulAtlas.
class FontManagerShared : public font_manager_ft {
public:
    static constexpr int kAtlasSize = 2048;  // headroom for CJK glyph sets

    FontManagerShared() {
        defaulAtlas = nullptr;
        // MSDF atlas: vector-crisp glyphs at any scale (median-of-3 in
        // the fragment shader) instead of the grayscale bitmap atlas.
        msdf_enabled = true;
        // Persist the MSDF atlas to the user cache dir — regenerating
        // ~350 glyphs through msdfgen costs ~8 s per process, which
        // dominates headless/test startup. The cache is keyed by font
        // path hash and lands in $XDG_CACHE_HOME/glyb (or
        // GLYB_ATLAS_CACHE_DIR).
        msdf_autoload = true;
    }

    font_atlas* getCurrentAtlas(font_face* face) override {
        if (!defaulAtlas) defaulAtlas = getNewAtlas(face);
        return defaulAtlas;
    }

    font_atlas* getNewAtlas(font_face* face) override {
        auto atlas = std::unique_ptr<font_atlas>(new font_atlas(0, 0, 0));
        auto ai = faceAtlasMap.find(face);
        if (ai == faceAtlasMap.end())
            ai = faceAtlasMap.insert(faceAtlasMap.end(),
                std::make_pair(face, std::vector<font_atlas*>()));
        if (face && msdf_enabled && msdf_autoload && ai->second.empty()) {
            atlas->load(this, face);
            importAtlas(atlas.get());
        }
        if (!atlas->pixels)
            atlas->reset(kAtlasSize, kAtlasSize,
                         color_enabled ? font_atlas::COLOR_DEPTH :
                         msdf_enabled  ? font_atlas::MSDF_DEPTH :
                                         font_atlas::GRAY_DEPTH);
        auto* atlasp = atlas.get();
        if (face) ai->second.push_back(atlasp);
        everyAtlas.push_back(std::move(atlas));
        return atlasp;
    }
};

/// text_shaper_hb subclass that guesses direction/script from content
/// instead of hardcoding LTR/Latin — enables RTL (Arabic/Hebrew) and
/// correct script-aware shaping. Adds no members: text_shaper_hb lacks
/// a virtual dtor, so this must stay stateless.
class ShaperGuess : public text_shaper_hb {
public:
    void shape(std::vector<glyph_shape>& shapes,
               text_segment& segment) override {
        auto* face = static_cast<font_face_ft*>(segment.face);
        face->get_metrics(segment.font_size);
        hb_font_t* hbfont = face->get_hbfont(segment.font_size);
        hb_language_t hblang = hb_language_from_string(
            segment.language.c_str(), (int)segment.language.size());
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_set_language(buf, hblang);
        hb_buffer_add_utf8(buf, segment.text.c_str(),
                           (int)segment.text.size(), 0,
                           (int)segment.text.size());
        hb_buffer_guess_segment_properties(buf);
        hb_shape(hbfont, buf, nullptr, 0);
        unsigned n = 0;
        auto* info = hb_buffer_get_glyph_infos(buf, &n);
        auto* pos = hb_buffer_get_glyph_positions(buf, &n);
        for (unsigned i = 0; i < n; ++i)
            shapes.push_back({info[i].codepoint, info[i].cluster,
                              pos[i].x_offset, pos[i].y_offset,
                              pos[i].x_advance, pos[i].y_advance});
        hb_buffer_destroy(buf);
    }
};

/// A maximal byte-range of text sharing one font face.
struct FontRun { size_t start; size_t len; font_face* face; };

/// Split a UTF-8 line into runs by covering face. Neutral codepoints
/// (space, ASCII punctuation) extend the current run rather than
/// splitting it — keeps "abc 中" to two runs instead of three.
std::vector<FontRun> splitFontRuns(std::string_view line,
                                   font_face* primary,
                                   font_face* fallback) {
    std::vector<FontRun> runs;
    size_t i = 0;
    while (i < line.size()) {
        auto uc = utf8_to_utf32_code(line.data() + i);
        if (uc.len <= 0) break;
        uint32_t cp = static_cast<uint32_t>(uc.code);
        font_face* f = faceFor(primary, fallback, cp);
        bool neutral = cp == ' ' ||
            (cp < 0x80 && std::ispunct(static_cast<unsigned char>(cp)));
        if (!runs.empty() && (neutral || f == runs.back().face)) {
            runs.back().len += size_t(uc.len);
        } else {
            runs.push_back({i, size_t(uc.len), f});
        }
        i += size_t(uc.len);
    }
    return runs;
}

/// Advance (in pixels) of a single-face run shaped at `fontSize`.
/// Positive even for RTL runs — direction only affects glyph order.
float runAdvance(text_shaper* shaper, font_face* face,
                 std::string_view text, int fontSize) {
    std::vector<glyph_shape> shapes;
    text_segment seg(std::string(text), "en", face, fontSize, 0, 0, 0);
    shaper->shape(shapes, seg);
    float adv = 0.0f;
    for (const auto& s : shapes) adv += s.x_advance / 64.0f;
    return std::abs(adv);
}

/// Shape `text` with `face` and render into `batch`. `penX` is the
/// run's left edge in screen pixels — for RTL runs (negative advance)
/// the glyph origin is placed at the run's right edge. Returns the
/// run's advance (always positive).
float shapeAndRenderRun(text_shaper* shaper, text_renderer_ft* renderer,
                        draw_list& batch, font_face* face,
                        std::string_view text, int fontSize,
                        float penX, float y, uint32_t rgba) {
    std::vector<glyph_shape> shapes;
    text_segment seg(std::string(text), "en", face, fontSize,
                     penX, y, rgba);
    shaper->shape(shapes, seg);
    float adv = 0.0f;
    for (const auto& s : shapes) adv += s.x_advance / 64.0f;
    if (adv < 0.0f) seg.x = penX - adv;  // RTL: origin at right edge
    renderer->render(batch, shapes, seg);
    return std::abs(adv);
}

} // namespace

// The atlas always stores glyphs rasterized at this fixed reference size;
// draw()/measureText() scale vertex positions and metrics from it. This
// keeps every glyph valid at every render scale (the one atlas upload
// covers all sizes).
static constexpr int kRefFontSize = int(16.0f * 64.0f);  // 16px, 26.6

float TextRenderer::lineHeight(float scale) {
    if (!fontFace_) return 16.0f * scale;
    auto* ftface = static_cast<font_face_ft*>(fontFace_);
    auto* m = ftface->get_metrics(kRefFontSize);
    float h = m->height / 64.0f;
    return (h > 0.0f ? h : 19.2f) * scale;
}

TextRenderer::TextMetrics
TextRenderer::measureText(std::string_view text, float scale) {
    if (!fontFace_ || text.empty()) return {0, 0, 0};
    auto* ftface = static_cast<font_face_ft*>(fontFace_);
    auto* m = ftface->get_metrics(kRefFontSize);
    float ascent = m->ascender / 64.0f;
    float descent = -m->descender / 64.0f;  // descender is negative
    float lineH = lineHeight(1.0f);

    float maxWidth = 0.0f;
    int lines = 0;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        auto line = text.substr(start, nl == std::string_view::npos
                                        ? nl : nl - start);
        ++lines;
        if (!line.empty()) {
            float width = 0.0f;
            for (const auto& r : splitFontRuns(line, fontFace_, fallbackFace_))
                width += runAdvance(shaper_.get(), r.face,
                                    line.substr(r.start, r.len), kRefFontSize);
            maxWidth = std::max(maxWidth, width);
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    float height = ascent + descent + (lines - 1) * lineH;
    return {maxWidth * scale, height * scale, ascent * scale};
}

TextRenderer::TextRenderer() = default;
TextRenderer::~TextRenderer() {
    delete static_cast<draw_list*>(batch_);
}

void TextRenderer::init(vk::Device device, VmaAllocator allocator,
                        vk::RenderPass renderPass,
                        vk::SampleCountFlagBits samples,
                        core::PipelineCache& /*cache*/,
                        core::DescriptorPool& /*descPool*/) {
    device_ = device;
    allocator_ = allocator;

    // --- glyb font manager, shaper, renderer ---
    fontManager_ = std::make_unique<FontManagerShared>();
    shaper_ = std::make_unique<ShaperGuess>();
    textRenderer_ = std::make_unique<text_renderer_ft>(fontManager_.get());
    batch_ = new draw_list();

    loadFont();

    // --- Vulkan pipeline for textured quads ---

    // Descriptor set layout: one combined image sampler (binding 0).
    vk::DescriptorSetLayoutBinding binding{};
    binding.setBinding(0)
           .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
           .setDescriptorCount(1)
           .setStageFlags(vk::ShaderStageFlagBits::eFragment);
    vk::DescriptorSetLayoutCreateInfo dslci{};
    dslci.setBindings(binding);
    descSetLayout_ = device.createDescriptorSetLayoutUnique(dslci);

    // Descriptor pool for the atlas texture.
    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1);
    vk::DescriptorPoolCreateInfo dpci{};
    dpci.setPoolSizes(poolSize)
        .setMaxSets(1)
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    descPool_ = device.createDescriptorPoolUnique(dpci);

    // Allocate descriptor set.
    vk::DescriptorSetLayout layouts[] = { descSetLayout_.get() };
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.setDescriptorPool(descPool_.get())
        .setSetLayouts(layouts);
    auto sets = device.allocateDescriptorSets(dsai);
    descSet_ = sets[0];

    // Sampler.
    vk::SamplerCreateInfo sci{};
    sci.setMagFilter(vk::Filter::eLinear)
       .setMinFilter(vk::Filter::eLinear)
       .setMipmapMode(vk::SamplerMipmapMode::eLinear)
       .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
       .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
       .setBorderColor(vk::BorderColor::eFloatTransparentBlack)
       .setUnnormalizedCoordinates(VK_FALSE);
    sampler_ = device.createSamplerUnique(sci);

    // Pipeline layout.
    vk::PushConstantRange pc{};
    pc.setStageFlags(vk::ShaderStageFlagBits::eVertex)
       .setOffset(0).setSize(sizeof(float) * 2);
    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(descSetLayout_.get())
        .setPushConstantRanges(pc);
    pipelineLayout_ = device.createPipelineLayoutUnique(plci);

    // Shaders.
    auto vertSpv = core::ShaderModule::compileGlsl(kVertGlsl, "vert");
    auto fragSpv = core::ShaderModule::compileGlsl(kFragGlsl, "frag");
    core::ShaderModule vertMod(device, vertSpv);
    core::ShaderModule fragMod(device, fragSpv);

    vk::PipelineShaderStageCreateInfo stages[2];
    stages[0].setStage(vk::ShaderStageFlagBits::eVertex)
             .setModule(vertMod.handle()).setPName("main");
    stages[1].setStage(vk::ShaderStageFlagBits::eFragment)
             .setModule(fragMod.handle()).setPName("main");

    // Vertex input: pos(2f), uv(2f), color(4f) = 32 bytes
    vk::VertexInputBindingDescription vbind{};
    vbind.setBinding(0).setStride(sizeof(TextVertex))
         .setInputRate(vk::VertexInputRate::eVertex);
    vk::VertexInputAttributeDescription vattrs[3];
    vattrs[0].setLocation(0).setBinding(0)
             .setFormat(vk::Format::eR32G32Sfloat).setOffset(0);
    vattrs[1].setLocation(1).setBinding(0)
             .setFormat(vk::Format::eR32G32Sfloat)
             .setOffset(offsetof(TextVertex, u));
    vattrs[2].setLocation(2).setBinding(0)
             .setFormat(vk::Format::eR32G32B32A32Sfloat)
             .setOffset(offsetof(TextVertex, r));

    vk::PipelineVertexInputStateCreateInfo visci{};
    visci.setVertexBindingDescriptions(vbind)
         .setVertexAttributeDescriptions(vattrs);

    vk::PipelineInputAssemblyStateCreateInfo iaci{};
    iaci.setTopology(vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo vsci{};
    vsci.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rsci{};
    rsci.setLineWidth(1.0f).setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone);

    vk::PipelineMultisampleStateCreateInfo msci{};
    msci.setRasterizationSamples(samples);

    vk::PipelineDepthStencilStateCreateInfo depthState{};
    depthState.setDepthTestEnable(false).setDepthWriteEnable(false);

    vk::PipelineColorBlendAttachmentState att{};
    att.setBlendEnable(true)
       .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
       .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorBlendOp(vk::BlendOp::eAdd)
       .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
       .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
       .setColorWriteMask(vk::ColorComponentFlagBits::eR
                        | vk::ColorComponentFlagBits::eG
                        | vk::ColorComponentFlagBits::eB
                        | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo cbsci{};
    cbsci.setAttachments(att);

    vk::DynamicState dynStates[] = { vk::DynamicState::eViewport,
                                     vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dsci{};
    dsci.setDynamicStates(dynStates);

    vk::GraphicsPipelineCreateInfo gpci{};
    gpci.setStages(stages)
        .setPVertexInputState(&visci)
        .setPInputAssemblyState(&iaci)
        .setPViewportState(&vsci)
        .setPRasterizationState(&rsci)
        .setPMultisampleState(&msci)
        .setPDepthStencilState(&depthState)
        .setPColorBlendState(&cbsci)
        .setPDynamicState(&dsci)
        .setLayout(pipelineLayout_.get())
        .setRenderPass(renderPass);

    auto rv = device.createGraphicsPipelineUnique({}, gpci);
    pipeline_ = std::move(rv.value);
    inited_ = true;
}

void TextRenderer::loadFont() {
    auto fontPath = findSystemFontFile();
    if (fontPath.empty()) return;
    fontManager_->scanFontPath(fontPath);
    fontFace_ = fontManager_->findFontByPath(fontPath);
    if (!fontFace_) return;

    // Broad-coverage fallback face for scripts the primary font lacks
    // (CJK, Arabic, Hebrew, …). glyb shares one atlas across faces, so
    // fallback glyphs land in the same GPU texture.
    if (!std::getenv("VOLCANO_FONT")) {
        std::string fbPath = findFallbackFontFile(fontManager_->ftlib,
                                                  fontPath);
        if (!fbPath.empty()) {
            fontManager_->scanFontPath(fbPath);
            fallbackFace_ = fontManager_->findFontByPath(fbPath);
        }
    }
}

void TextRenderer::resetScratch() {
    // Reset ring-buffer offsets for the new frame.
    vbOffset_ = 0;
    ibOffset_ = 0;
}

void TextRenderer::ensureScratch(size_t vertexBytes, size_t indexBytes) {
    // Ring-buffered: ensure total capacity is enough for the current frame.
    if (vertexBytes > vbCapacity_) {
        size_t newSize = std::max<size_t>(65536, vertexBytes * 2);
        core::BufferDesc bdesc{};
        bdesc.size = newSize;
        bdesc.usage = core::BufferUsage::Vertex;
        bdesc.hostVisible = true;
        scratchVB_ = core::Buffer(allocator_, bdesc);
        vbCapacity_ = newSize;
    }
    if (indexBytes > ibCapacity_) {
        size_t newSize = std::max<size_t>(65536, indexBytes * 2);
        core::BufferDesc bdesc{};
        bdesc.size = newSize;
        bdesc.usage = core::BufferUsage::Index;
        bdesc.hostVisible = true;
        scratchIB_ = core::Buffer(allocator_, bdesc);
        ibCapacity_ = newSize;
    }
}

void TextRenderer::prepareAtlas(vk::Queue queue, vk::CommandPool pool) {
    if (atlasUploaded_ || !fontFace_) return;
    auto* atlas = fontManager_->getCurrentAtlas(fontFace_);
    prepareAtlasGlyphs();
    // Persist freshly generated atlases so the next process skips
    // msdfgen entirely. Atlases restored from the on-disk cache aren't
    // re-saved (resized per-size entries share the template's bins and
    // don't need persisting).
    if (atlas && !atlas->loadedFromDisk)
        atlas->save(fontManager_.get(), fontFace_);
    uploadAtlas(queue, pool);
    atlasUploaded_ = true;
    atlasGlyphCount_ = fontManager_->glyph_map.size();
}

void TextRenderer::syncAtlas(vk::Queue queue, vk::CommandPool pool) {
    if (!atlasDirty_ || !atlasUploaded_) return;
    uploadAtlas(queue, pool);
    atlasDirty_ = false;
    atlasGlyphCount_ = fontManager_->glyph_map.size();
}

/// Prerender the ASCII + math-symbol charset so common text is already
/// in the atlas before the first frame. Extended-script glyphs (CJK,
/// Arabic, …) are still rasterized lazily on first use — draw() marks
/// the atlas dirty and the renderer re-uploads between frames.
void TextRenderer::prepareAtlasGlyphs() {

    // Pre-render common characters to populate the atlas: ASCII plus the
    // Greek letters, math symbols, and combining accents emitted by the
    // MathText engine (the GPU atlas texture is uploaded once, so all
    // needed glyphs must be rasterized here).
    auto* batch = static_cast<draw_list*>(batch_);
    std::string charset;
    for (char c = 32; c < 127; ++c) charset.push_back(c);
    charset += "αβγδϵεζηθϑικλμνξπϖρϱσςτυϕφχψω"
               "ΓΔΘΛΞΠΣΥΦΨΩ"
               "×÷±∓⋅⋯…≤≥≠≈≃≅≡∼∝≪≫∈∉∋⊂⊃⊆⊇∪∩∖∅∀∃∄¬∧∨"
               "→←⇒⇐↔⇔↦↑↓∑∏∐∫∬∭∮⋃⋂⨁⨂⨀⨄⨆⋁⋀∂∇∞ℏℓℜℑ℘ℵıȷ"
               "°∘•⋆∗′⊕⊖⊗⊘⊙⊥∥∠△□■⋄⟨⟩⌈⌉⌊⌋∣⊤⊢⊣⊨∴∵√"
               "̂̃̄̇̈⃗̆̌́̀";

    int font_size = int(16.0f * 64.0f);  // 16px in 26.6 fixed-point
    std::string lang = "en";
    text_segment segment(charset, lang, fontFace_, font_size, 0, 0, 0xff000000);

    std::vector<glyph_shape> shapes;
    draw_list_clear(*batch);
    shaper_->shape(shapes, segment);
    textRenderer_->render(*batch, shapes, segment);
}

void TextRenderer::uploadAtlas(vk::Queue queue, vk::CommandPool pool) {
    // Get the atlas (populated with glyphs).
    auto* atlas = fontManager_->getCurrentAtlas(fontFace_);
    if (!atlas || !atlas->pixels) return;

    atlasWidth_ = (int)atlas->width;
    atlasHeight_ = (int)atlas->height;

    if (!atlasUploaded_) {
        // Create the Vulkan image (RGBA8_UNORM for the MSDF atlas).
        core::ImageDesc idesc{};
        idesc.format = vk::Format::eR8G8B8A8Unorm;
        idesc.extent = vk::Extent2D{uint32_t(atlasWidth_), uint32_t(atlasHeight_)};
        idesc.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        idesc.tiling = vk::ImageTiling::eOptimal;
        atlasImage_ = core::Image(allocator_, idesc);

        // Create image view.
        vk::ImageViewCreateInfo ivci{};
        ivci.setImage(atlasImage_.handle())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setSubresourceRange(vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        atlasView_ = device_.createImageViewUnique(ivci);
    }

    // Create a staging buffer and copy atlas pixels.
    size_t pixelBytes = size_t(atlasWidth_) * size_t(atlasHeight_) * 4;
    core::BufferDesc sdesc{};
    sdesc.size = pixelBytes;
    sdesc.usage = core::BufferUsage::Staging;
    sdesc.hostVisible = true;
    core::Buffer staging(allocator_, sdesc);
    std::memcpy(staging.mappedData(), atlas->pixels, pixelBytes);

    // Use a one-time command buffer (OUTSIDE any render pass).
    vk::CommandBufferAllocateInfo cbai{};
    cbai.setCommandPool(pool)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);
    auto cmds = device_.allocateCommandBuffers(cbai);
    vk::CommandBuffer cmd = cmds[0];

    vk::CommandBufferBeginInfo cbbi{};
    cbbi.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd.begin(cbbi);

    // Transition image to transfer dst (ShaderReadOnly on re-upload).
    core::Image::transitionLayout(cmd, atlasImage_.handle(),
        vk::Format::eR8G8B8A8Unorm,
        atlasUploaded_ ? vk::ImageLayout::eShaderReadOnlyOptimal
                       : vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal);

    // Copy buffer to image.
    vk::BufferImageCopy region{};
    region.setBufferOffset(0)
          .setBufferRowLength(atlasWidth_)
          .setBufferImageHeight(atlasHeight_)
          .setImageSubresource(vk::ImageSubresourceLayers{
              vk::ImageAspectFlagBits::eColor, 0, 0, 1})
          .setImageOffset(vk::Offset3D{0, 0, 0})
          .setImageExtent(vk::Extent3D{uint32_t(atlasWidth_), uint32_t(atlasHeight_), 1});
    cmd.copyBufferToImage(staging.handle(), atlasImage_.handle(),
                          vk::ImageLayout::eTransferDstOptimal, region);

    // Transition image to shader read.
    core::Image::transitionLayout(cmd, atlasImage_.handle(),
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal);

    cmd.end();

    // Submit and wait.
    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(cmd);
    queue.submit(submitInfo);
    queue.waitIdle();

    // Free the command buffer.
    device_.freeCommandBuffers(pool, cmd);

    if (!atlasUploaded_) {
        // Update descriptor set (image/view are stable across re-uploads).
        vk::DescriptorImageInfo dii{};
        dii.setSampler(sampler_.get())
           .setImageView(atlasView_.get())
           .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet wds{};
        wds.setDstSet(descSet_)
           .setDstBinding(0)
           .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
           .setImageInfo(dii);
        device_.updateDescriptorSets(wds, {});
    }
}

void TextRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect,
                        std::string_view text, float x, float y,
                        plot::Color color, float scale, float rotation,
                        plot::HAlign lineAlign) {
    if (!inited_ || !fontFace_ || text.empty()) return;

    // Glyphs are shaped at the fixed reference size (the atlas stores 16px
    // bitmaps); `scale` is applied to vertex positions below.
    constexpr int font_size = kRefFontSize;
    uint32_t rgba = (uint32_t(color.r * 255) << 24) |
                    (uint32_t(color.g * 255) << 16) |
                    (uint32_t(color.b * 255) << 8)  |
                    (uint32_t(color.a * 255));

    auto* batch = static_cast<draw_list*>(batch_);
    draw_list_clear(*batch);

    // Split into lines; each line is shaped at its own offset so the
    // whole block rotates around (x, y). All layout offsets are in
    // unscaled reference space — `scale` is applied to vertices below.
    float lineH = lineHeight(1.0f);
    float blockW = lineAlign == plot::HAlign::Left
                       ? 0.0f : measureText(text, 1.0f).width;
    float lineY = y;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        auto line = text.substr(start, nl == std::string_view::npos
                                        ? nl : nl - start);
        if (!line.empty()) {
            auto runs = splitFontRuns(line, fontFace_, fallbackFace_);
            float lineX = x;
            if (lineAlign != plot::HAlign::Left) {
                // Per-line alignment within the block width.
                float lw = 0.0f;
                for (const auto& r : runs)
                    lw += runAdvance(shaper_.get(), r.face,
                                     line.substr(r.start, r.len), font_size);
                lineX += lineAlign == plot::HAlign::Center
                             ? (blockW - lw) * 0.5f : (blockW - lw);
            }
            float pen = lineX;
            for (const auto& r : runs)
                pen += shapeAndRenderRun(shaper_.get(), textRenderer_.get(),
                                         *batch, r.face,
                                         line.substr(r.start, r.len),
                                         font_size, pen, lineY, rgba);
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
        lineY += lineH;
    }

    // Lazily-rasterized glyphs (CJK, fallback-face runs, …) grow the CPU
    // atlas — flag it so the renderer re-uploads before the next frame.
    if (fontManager_->glyph_map.size() != atlasGlyphCount_) {
        atlasGlyphCount_ = fontManager_->glyph_map.size();
        if (atlasUploaded_) atlasDirty_ = true;
    }

    if (batch->vertices.empty() || batch->indices.empty()) return;

    // Atlas must have been uploaded via prepareAtlas() before any draw calls.
    if (!atlasUploaded_) return;

    // Convert glyb draw_vertex to our TextVertex.
    // glyb vertex: pos[3], uv[2], color(u32), shape(f32)
    // Our vertex: x, y, u, v, r, g, b, a
    size_t vertCount = batch->vertices.size();
    size_t idxCount = batch->indices.size();
    size_t vertBytes = vertCount * sizeof(TextVertex);
    size_t idxBytes = idxCount * sizeof(uint32_t);

    // Ring-buffer the scratch VB/IB so that multiple text draw calls in the
    // same frame don't overwrite each other (GPU reads the data later).
    if (vbOffset_ + vertBytes > vbCapacity_ ||
        ibOffset_ + idxBytes > ibCapacity_) {
        // Not enough room — grow the buffers (reallocates, losing prior frame
        // data, but we're mid-frame so that's a bug; for now just skip).
        // In practice the capacity is large enough for a full frame of text.
        ensureScratch(vbOffset_ + vertBytes, ibOffset_ + idxBytes);
    }

    // Convert vertices, applying scale and rotation around the text
    // origin (x, y). Glyph bitmaps are the fixed 16px reference size;
    // `scale` stretches the quads (GPU-side upsampling).
    auto* dstVerts = static_cast<TextVertex*>(scratchVB_.mappedData()) + vbOffset_ / sizeof(TextVertex);
    float cosR = std::cos(rotation) * scale;
    float sinR = std::sin(rotation) * scale;
    for (size_t i = 0; i < vertCount; ++i) {
        const auto& sv = batch->vertices[i];
        float px = sv.pos[0];
        float py = sv.pos[1];
        // Scale + rotate (px, py) around origin (x, y).
        float dx = px - x;
        float dy = py - y;
        dstVerts[i].x = x + dx * cosR - dy * sinR;
        dstVerts[i].y = y + dx * sinR + dy * cosR;
        dstVerts[i].u = sv.uv[0];
        dstVerts[i].v = sv.uv[1];
        // glyb color is RGBA8 packed as uint32.
        uint32_t c = sv.color;
        dstVerts[i].r = ((c >> 24) & 0xff) / 255.0f;
        dstVerts[i].g = ((c >> 16) & 0xff) / 255.0f;
        dstVerts[i].b = ((c >> 8) & 0xff) / 255.0f;
        dstVerts[i].a = (c & 0xff) / 255.0f;
    }

    // Copy indices. No adjustment needed — the VB is bound at this draw's
    // vertex offset, so 0-based indices from glyb are correct.
    auto* dstIdx = static_cast<uint32_t*>(scratchIB_.mappedData()) + ibOffset_ / sizeof(uint32_t);
    std::memcpy(dstIdx, batch->indices.data(), idxBytes);

    // Advance ring buffer offsets.
    vbOffset_ += vertBytes;
    ibOffset_ += idxBytes;

    // Bind pipeline and descriptor set.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.get());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           pipelineLayout_.get(), 0, descSet_, {});

    // Push constant: framebuffer resolution.
    struct PC { float w, h; } pc{float(rect.extent.width), float(rect.extent.height)};
    cmd.pushConstants(pipelineLayout_.get(), vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(PC), &pc);

    // Bind vertex and index buffers with the correct offsets.
    cmd.bindVertexBuffers(0, scratchVB_.handle(), vk::DeviceSize(vbOffset_ - vertBytes));
    cmd.bindIndexBuffer(scratchIB_.handle(), vk::DeviceSize(ibOffset_ - idxBytes), vk::IndexType::eUint32);

    // Set viewport + scissor.
    vk::Viewport viewport{0, 0, float(rect.extent.width), float(rect.extent.height), 0, 1};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, rect);

    // Draw this text's indices only.
    cmd.drawIndexed(uint32_t(idxCount), 1, 0, 0, 0);
}

} // namespace volcano::text
