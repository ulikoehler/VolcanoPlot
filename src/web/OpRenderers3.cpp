// src/web/OpRenderers3.cpp — op-recording impls, part 3
// (heatmap, surface, instanced, gpuline, reduce, eval, kde, grid3d, text)
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

#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Colormap.hpp>

#include <cstring>

namespace volcano::web {

using render::Cmd;
using render::GpuBuf;
using namespace plot;
namespace pr = render::primitives;

namespace {
[[nodiscard]] inline Rect2Df clipF(Rect2D r) { return toF(r); }
} // namespace

// ═══ OpHeatmapRenderer ═══════════════════════════════════════════════

class OpHeatmapRenderer final : public pr::HeatmapRenderer {
public:
    explicit OpHeatmapRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(const Grid2D& grid, const Colormap& cmap,
                bool nanTransparent) override {
        nanTransparent_ = nanTransparent;
        w_ = grid.width; h_ = grid.height;
        rgbaMode_ = !grid.rgba.empty();
        xRange_ = grid.xRange; yRange_ = grid.yRange;
        valueRange_ = grid.valueRange;
        originLower_ = grid.origin == "lower";

        // Grid texture: rgba8 or r32float.
        if (gridTex_) s_->releaseTexture(gridTex_);
        gridTex_ = s_->createTextureRaw(w_, h_, rgbaMode_ ? 1 : 2);
        if (rgbaMode_)
            s_->writeTextureRaw(gridTex_, 0, 0, w_, h_,
                                grid.rgba.data(), grid.rgba.size() * 4);
        else
            s_->writeTextureRaw(gridTex_, 0, 0, w_, h_,
                                grid.values.data(),
                                grid.values.size() * 4);

        // Colormap LUT (256×1 rgba8) — same lookup as the Vulkan impl.
        if (cmapTex_) s_->releaseTexture(cmapTex_);
        cmapTex_ = s_->createTextureRaw(256, 1, 1);
        std::vector<uint8_t> lut(256 * 4);
        for (uint32_t i = 0; i < 256; ++i) {
            auto c = cmap.sample(i / 255.0f);
            lut[i*4+0] = uint8_t(c.r * 255);
            lut[i*4+1] = uint8_t(c.g * 255);
            lut[i*4+2] = uint8_t(c.b * 255);
            lut[i*4+3] = uint8_t(c.a * 255);
        }
        s_->writeTextureRaw(cmapTex_, 0, 0, 256, 1, lut.data(), lut.size());
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t) const override {
        if (!gridTex_) return;
        PDrawImage p{clipF(rect), makeTransformUBO(t, rect, {}),
                     gridTex_, cmapTex_,
                     {float(rgbaMode_), float(originLower_),
                      float(nanTransparent_),
                      valueRange_.min, valueRange_.max,
                      xRange_.min, yRange_.min, float(w_)}};
        // valueRange/gridRange detail — interpreter packs remaining
        // fields from TexParams; keep payload compact here.
        (void)p;
        ops(cmd).emit(Op::DrawImage, p);
    }
private:
    OpGpuServices* s_;
    uint32_t gridTex_ = 0, cmapTex_ = 0;
    uint32_t w_ = 0, h_ = 0;
    bool rgbaMode_ = false, originLower_ = false, nanTransparent_ = false;
    Range xRange_{}, yRange_{}, valueRange_{};
};

// ═══ OpSurfaceRenderer (v1: vertices only — WGSL port in M2) ════════

class OpSurfaceRenderer final : public pr::SurfaceRenderer {
public:
    explicit OpSurfaceRenderer(OpGpuServices& s) : s_(&s) {}
    void upload(const Grid2D&) override {}
    void draw(Cmd&, Rect2D, const Camera3D&, bool,
              float, float) const override {}
private:
    OpGpuServices* s_;
};

// ═══ OpInstancedPathRenderer ═════════════════════════════════════════

class OpInstancedPathRenderer final : public pr::InstancedPathRenderer {
public:
    explicit OpInstancedPathRenderer(OpGpuServices& s) : s_(&s) {}

    bool inited() const noexcept override { return true; }
    uint32_t templateVertCount() const noexcept override {
        return tplVerts_;
    }
    void setTemplate(std::span<const Point2D> triVerts) override {
        tpl_.assign(triVerts.begin(), triVerts.end());
        tplVerts_ = uint32_t(tpl_.size());
    }
    void drawInstanced(Cmd& cmd, Rect2D clip, Extent2D res,
                       std::span<const pr::PathInstance> instances) override {
        if (tpl_.empty() || instances.empty()) return;
        TransformUBO ubo{};
        ubo.rectW = float(res.width); ubo.rectH = float(res.height);
        PDrawInstanced p{clipF(clip),
                         ops(cmd).arenaCopy(
                             std::span<const Point2D>{tpl_}),
                         ops(cmd).arenaCopy(instances), ubo};
        ops(cmd).emit(Op::DrawInstanced, p);
    }
    void resetScratch() override {}
private:
    OpGpuServices* s_;
    std::vector<Point2D> tpl_;
    uint32_t tplVerts_ = 0;
};

// ═══ OpGpuLineRenderer (TessLines emit; mailbox deferred) ════════════

class OpGpuLineRenderer final : public pr::GpuLineRenderer {
public:
    explicit OpGpuLineRenderer(OpGpuServices& s) : s_(&s) {}

    std::vector<Mesh> tessellate(Cmd& cmd,
                               std::span<const Point2D> px,
                               const StrokeParams& sp,
                               Color color) override {
        if (px.size() < 2) return {};
        auto& os = ops(cmd);
        uint32_t inBuf = s_->createBufferRaw(px.size_bytes() + 16, 1);
        s_->writeBufferRaw(inBuf, 0, px.data(), px.size_bytes());
        // Worst-case tess output: 6 verts/segment (join may add).
        uint64_t outBytes = uint64_t(px.size()) * 6 * 32 + 64;
        uint32_t outBuf = s_->createBufferRaw(outBytes, 1);
        PTessLines p{inBuf, 0, outBuf, 0,
                     uint32_t(px.size()), uint32_t(px.size() - 1),
                     sp.width * 0.5f,
                     uint8_t(sp.join), uint8_t(sp.cap), sp.miterLimit,
                     color.r, color.g, color.b, color.a};
        os.emit(Op::TessLines, p);
        // The interpreter fills in the real vertex count after
        // dispatch; v1 assumes the 6-verts/segment upper bound.
        return {Mesh{GpuBuf(outBuf), 0,
                     uint32_t(px.size() - 1) * 6}};
    }
    bool envelopeColumns(GpuBuf, uint32_t, float, float, int, int,
                         std::vector<float>&,
                         std::vector<float>&) override { return false; }
    void resetScratch() override {}
    bool inited() const noexcept override { return true; }
private:
    OpGpuServices* s_;
};

// ═══ OpReduceRenderer (async — v1 returns nullopt) ═══════════════════

class OpReduceRenderer final : public pr::ReduceRenderer {
public:
    explicit OpReduceRenderer(OpGpuServices& s) : s_(&s) {}
    std::optional<pr::MinMax2D> reduceMinMax2D(GpuBuf buf,
                                         uint32_t count) override {
        // Emit the op with a mailbox slot; the JS interpreter writes
        // the result into WASM memory next frame (§6). v1: report
        // unavailable → CPU fallback keeps correctness.
        (void)buf; (void)count;
        return std::nullopt;
    }
    bool ready() const noexcept override { return false; }
private:
    OpGpuServices* s_;
};

// ═══ OpEvalRenderer ══════════════════════════════════════════════════

class OpEvalRenderer final : public pr::EvalRenderer {
public:
    explicit OpEvalRenderer(OpGpuServices& s) : s_(&s) {}

    bool compile(const std::string& body) override {
        funcId_ = nextFuncId_++;
        body_ = body;
        compiled_ = true;
        // FuncDef is emitted lazily at first eval (needs a stream).
        return true;
    }
    void eval(GpuBuf out, double xMin, double xMax,
              uint32_t count) override {
        if (!compiled_ || !s_->curStream()) return;
        auto& os = *s_->curStream();
        if (!defSent_) {
            PFuncDef d{funcId_, /*glsl-expr*/0,
                       os.arenaCopy(body_.data(), body_.size())};
            os.emit(Op::FuncDef, d);
            defSent_ = true;
        }
        PEvalFunc p{uint32_t(out), xMin, xMax, count, funcId_, {}};
        os.emit(Op::EvalFunc, p);
    }
    GpuBuf makeOutput(uint32_t count) const override {
        return GpuBuf(const_cast<OpGpuServices*>(s_)
                      ->createBufferRaw(uint64_t(count) * 8 + 16, 1));
    }
    bool ready() const noexcept override { return true; }
    bool compiled() const noexcept override { return compiled_; }
private:
    OpGpuServices* s_;
    static inline uint16_t nextFuncId_ = 1;
    uint16_t funcId_ = 0;
    std::string body_;
    bool compiled_ = false, defSent_ = false;
};

// ═══ OpKdeEvalRenderer (async — v1 CPU fallback) ═════════════════════

class OpKdeEvalRenderer final : public pr::KdeEvalRenderer {
public:
    explicit OpKdeEvalRenderer(OpGpuServices& s) : s_(&s) {}
    std::vector<float> eval(const std::vector<Point2D>&, uint32_t,
                            uint32_t, float, float, float, float,
                            float, float) override { return {}; }
    bool ready() const noexcept override { return false; }
private:
    OpGpuServices* s_;
};

namespace op {
std::unique_ptr<render::primitives::HeatmapRenderer>
    makeHeatmap(OpGpuServices& s) {
    return std::make_unique<OpHeatmapRenderer>(s);
}
std::unique_ptr<render::primitives::SurfaceRenderer>
    makeSurface(OpGpuServices& s) {
    return std::make_unique<OpSurfaceRenderer>(s);
}
std::unique_ptr<render::primitives::InstancedPathRenderer>
    makeInstancedPath(OpGpuServices& s) {
    return std::make_unique<OpInstancedPathRenderer>(s);
}
std::unique_ptr<render::primitives::GpuLineRenderer>
    makeGpuLine(OpGpuServices& s) {
    return std::make_unique<OpGpuLineRenderer>(s);
}
std::unique_ptr<render::primitives::ReduceRenderer>
    makeReduce(OpGpuServices& s) {
    return std::make_unique<OpReduceRenderer>(s);
}
std::unique_ptr<render::primitives::EvalRenderer>
    makeEval(OpGpuServices& s) {
    return std::make_unique<OpEvalRenderer>(s);
}
std::unique_ptr<render::primitives::KdeEvalRenderer>
    makeKdeEval(OpGpuServices& s) {
    return std::make_unique<OpKdeEvalRenderer>(s);
}
} // namespace op

} // namespace volcano::web
