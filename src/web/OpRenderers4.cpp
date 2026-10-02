// src/web/OpRenderers4.cpp — grid3D stub + op text renderer (v1 stub)
#include "OpGpuServices.hpp"
#include "OpCmd.hpp"
#include "OpPayloads.hpp"
#include "OpFactory.hpp"
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/render/primitives/PointRenderer.hpp>
#include <volcano/render/primitives/LineRenderer.hpp>
#include <volcano/render/primitives/LineSegmentRenderer.hpp>
#include <volcano/render/primitives/FillRenderer.hpp>
#include <volcano/render/primitives/BarRenderer.hpp>
#include <volcano/render/primitives/PieRenderer.hpp>
#include <volcano/render/primitives/HeatmapRenderer.hpp>
#include <volcano/render/primitives/SurfaceRenderer.hpp>
#include <volcano/render/primitives/InstancedPathRenderer.hpp>
#include <volcano/render/primitives/GpuLineRenderer.hpp>
#include <volcano/render/primitives/ReduceRenderer.hpp>
#include <volcano/render/primitives/EvalRenderer.hpp>
#include <volcano/render/primitives/KdeEvalRenderer.hpp>

#include <volcano/render/Grid3DRenderer.hpp>

#include <cmath>
#include <cstring>
#include <vector>

namespace volcano::web {

using render::Cmd;
using namespace plot;

// ═══ OpGrid3DRenderer (v1 stub — WGSL port in M2) ════════════════════

class OpGrid3DRenderer final : public render::Grid3DRenderer {
public:
    explicit OpGrid3DRenderer(OpGpuServices&) {}
    void draw(Cmd&, Rect2D, const Viewport&, const Camera3D&,
              const render::Grid3DStyle&) const override {}
};

// ═══ OpTextRenderer ══════════════════════════════════════════════════
// glyb CPU shaping/rasterization (shared base) → atlas bitmap via
// WriteTexture → triangle-soup quads via DrawTextQuads. The atlas is
// grayscale under Emscripten (depth 1 → r8unorm); lazy glyph growth
// re-uploads the bitmap, and dimension changes re-create the texture.

class OpTextRenderer final : public text::TextRenderer {
public:
    explicit OpTextRenderer(OpGpuServices& s) : s_(&s) { initFonts(); }

    void prepareAtlasGpu() override {
        prepareAtlasGlyphs();
        uploadAtlas();
        atlasDirty_ = false;
    }
    void resetScratch() override {}
    void syncAtlas() override {
        if (!atlasDirty_) return;
        uploadAtlas();
        atlasDirty_ = false;
    }

    void draw(render::Cmd& cmd, Rect2D rect, std::string_view text,
              float x, float y, Color color, float scale, float rotation,
              HAlign lineAlign, font_face* face) override {
        uint32_t rgba = (uint32_t(color.r * 255) << 24) |
                        (uint32_t(color.g * 255) << 16) |
                        (uint32_t(color.b * 255) << 8)  |
                        (uint32_t(color.a * 255));
        if (!shapeText(text, x, y, rgba, lineAlign, face)) return;
        // New glyphs leave atlasDirty_ set; Renderer::renderFrame calls
        // syncAtlas() and repaints (same protocol as the Vulkan impl).
        if (!atlasTex_) return;

        // Expand indexed quads → triangle soup of 32 B records
        // {x, y, u, v, r, g, b, a}, applying scale+rotation about (x, y).
        auto verts = batchVertices();
        auto idx = batchIndices();
        quads_.clear();
        quads_.reserve(idx.size() * 8);
        float cosR = std::cos(rotation) * scale;
        float sinR = std::sin(rotation) * scale;
        for (uint32_t i : idx) {
            const GlyphVert& sv = verts[i];
            float dx = sv.px - x, dy = sv.py - y;
            quads_.push_back(x + dx * cosR - dy * sinR);
            quads_.push_back(y + dx * sinR + dy * cosR);
            quads_.push_back(sv.u);
            quads_.push_back(sv.v);
            uint32_t c = sv.color;
            quads_.push_back(((c >> 24) & 0xff) / 255.0f);
            quads_.push_back(((c >> 16) & 0xff) / 255.0f);
            quads_.push_back(((c >> 8) & 0xff) / 255.0f);
            quads_.push_back((c & 0xff) / 255.0f);
        }

        PDrawTextQuads p{toF(rect), atlasTex_,
                         ops(cmd).arenaCopy(
                             std::span<const float>{quads_})};
        ops(cmd).emit(Op::DrawTextQuads, p);
    }

private:
    void uploadAtlas() {
        int w, h, depth;
        const void* px = atlasPixels(w, h, depth);
        if (!px) return;
        if (atlasTex_ && (w != atlasW_ || h != atlasH_)) {
            s_->releaseTexture(atlasTex_);
            atlasTex_ = 0;
        }
        if (!atlasTex_) {
            atlasTex_ = s_->createTextureRaw(uint32_t(w), uint32_t(h),
                                             depth == 1 ? 0 : 1);
            atlasW_ = w; atlasH_ = h;
        }
        s_->writeTextureRaw(atlasTex_, 0, 0, uint32_t(w), uint32_t(h),
                            px, size_t(w) * size_t(h) * size_t(depth));
    }

    OpGpuServices* s_;
    uint32_t atlasTex_ = 0;
    int atlasW_ = 0, atlasH_ = 0;
    std::vector<float> quads_;
};

namespace op {
std::unique_ptr<render::Grid3DRenderer>
    makeGrid3D(OpGpuServices& s) {
    return std::make_unique<OpGrid3DRenderer>(s);
}
std::unique_ptr<text::TextRenderer>
    makeText(OpGpuServices& s) {
    return std::make_unique<OpTextRenderer>(s);
}
} // namespace op

} // namespace volcano::web
