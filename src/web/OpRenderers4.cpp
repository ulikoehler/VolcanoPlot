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

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace volcano::web {

using render::Cmd;
using namespace plot;

// ═══ OpGrid3DRenderer — ray-cast grid planes → DrawGrid3D op ════════
// The 44-float block mirrors Grid3DRendererVk::draw's push constants
// verbatim; the WGSL shader does the same plane-intersect math.

namespace {
/// 4x4 row-major inverse via Gauss-Jordan (same as Grid3DRenderer.cpp).
std::array<float, 16> mat4Inverse(const std::array<float, 16>& m) {
    std::array<float, 32> aug;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            aug[i * 8 + j] = m[i * 4 + j];
            aug[i * 8 + 4 + j] = (i == j) ? 1.0f : 0.0f;
        }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        float maxVal = std::abs(aug[col * 8 + col]);
        for (int row = col + 1; row < 4; ++row)
            if (std::abs(aug[row * 8 + col]) > maxVal) {
                maxVal = std::abs(aug[row * 8 + col]); pivot = row;
            }
        if (maxVal < 1e-30f) return {};
        if (pivot != col)
            for (int j = 0; j < 8; ++j)
                std::swap(aug[col * 8 + j], aug[pivot * 8 + j]);
        const float piv = aug[col * 8 + col];
        for (int j = 0; j < 8; ++j) aug[col * 8 + j] /= piv;
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const float f = aug[row * 8 + col];
            for (int j = 0; j < 8; ++j) aug[row * 8 + j] -= f * aug[col * 8 + j];
        }
    }
    std::array<float, 16> inv{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) inv[i * 4 + j] = aug[i * 8 + 4 + j];
    return inv;
}
} // namespace

class OpGrid3DRenderer final : public render::Grid3DRenderer {
public:
    explicit OpGrid3DRenderer(OpGpuServices&) {}
    void draw(Cmd& cmd, Rect2D rect, const Viewport& viewport,
              const Camera3D& camera,
              const render::Grid3DStyle& style) const override {
        PDrawGrid3D p{};
        p.clip = toF(rect);
        float* pc = p.pc;
        pc[0] = float(rect.x); pc[1] = float(rect.y);
        pc[2] = float(rect.width); pc[3] = float(rect.height);
        pc[4] = viewport.x.min; pc[5] = viewport.x.min;
        pc[6] = viewport.x.span(); pc[7] = 0;
        pc[8] = viewport.y.min; pc[9] = viewport.y.min;
        pc[10] = viewport.y.span(); pc[11] = 0;
        pc[12] = viewport.z.min; pc[13] = viewport.z.min;
        pc[14] = viewport.z.span(); pc[15] = 0;
        pc[16] = style.color.r; pc[17] = style.color.g;
        pc[18] = style.color.b; pc[19] = style.color.a;
        pc[20] = style.floorXZ ? 1.f : 0.f;
        pc[21] = style.backWallXY ? 1.f : 0.f;
        pc[22] = style.sideWallYZ ? 1.f : 0.f;
        pc[23] = style.step;
        const auto inv = mat4Inverse(camera.viewProjection());
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                pc[24 + i * 4 + j] = inv[i * 4 + j];
        pc[40] = camera.eye.x; pc[41] = camera.eye.y;
        pc[42] = camera.eye.z; pc[43] = 0;
        ops(cmd).emit(Op::DrawGrid3D, p);
    }
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
