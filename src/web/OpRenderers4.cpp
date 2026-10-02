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
// v1: CPU metrics work (initFonts/measureText via glyb); draw() is a
// no-op stub. M2 wires the glyb atlas bitmap → WriteTexture →
// DrawTextQuads path (WEBGPU-PLAN §4 text notes, §6 atlasDirty).

class OpTextRenderer final : public text::TextRenderer {
public:
    explicit OpTextRenderer(OpGpuServices& s) : s_(&s) { initFonts(); }

    void prepareAtlasGpu() override {}
    void resetScratch() override {}
    void syncAtlas() override { atlasDirty_ = false; }
    void draw(render::Cmd&, Rect2D, std::string_view, float, float,
              Color, float, float, HAlign,
              font_face*) override {
        // TODO(M2): atlas upload + DrawTextQuads emit
    }

private:
    OpGpuServices* s_;
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
