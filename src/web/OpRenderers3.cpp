// src/web/OpRenderers3.cpp — op-recording impls, part 3
// (heatmap, surface, instanced, gpuline, reduce, eval, kde, grid3d, text)
#include "OpGpuServices.hpp"
#include <volcano/render/Offload.hpp>
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

#include <cmath>
#include <cstring>
#include <vector>

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

// ═══ OpSurfaceRenderer — CPU grid tessellation → DrawSurface op ═════
// Same vertex/index construction as SurfaceRendererVk::upload; the VP
// matrix and shading parameters travel in the op payload.

class OpSurfaceRenderer final : public pr::SurfaceRenderer {
public:
    explicit OpSurfaceRenderer(OpGpuServices& s) : s_(&s) {}
    void upload(const Grid2D& grid) override {
        if (grid.width < 2 || grid.height < 2) { indexCount_ = 0; return; }
        std::vector<Point3D> verts(grid.width * grid.height);
        const float xMin = grid.xRange.min, xMax = grid.xRange.max;
        const float yMin = grid.yRange.min, yMax = grid.yRange.max;
        for (uint32_t j = 0; j < grid.height; ++j)
            for (uint32_t i = 0; i < grid.width; ++i) {
                float x = xMin + float(i) / float(grid.width - 1)
                                * (xMax - xMin);
                float y = yMin + float(j) / float(grid.height - 1)
                                * (yMax - yMin);
                verts[j * grid.width + i] = {x, y,
                                             grid.values[j * grid.width + i]};
            }
        std::vector<uint32_t> indices;
        indices.reserve((grid.width - 1) * (grid.height - 1) * 6);
        for (uint32_t j = 0; j < grid.height - 1; ++j)
            for (uint32_t i = 0; i < grid.width - 1; ++i) {
                uint32_t a = j * grid.width + i;
                uint32_t b = a + 1, c = a + grid.width, d = c + 1;
                indices.insert(indices.end(), {a, c, b, b, c, d});
            }
        indexCount_ = uint32_t(indices.size());
        if (vertBuf_) s_->releaseBuffer(vertBuf_);
        if (idxBuf_) s_->releaseBuffer(idxBuf_);
        vertBuf_ = s_->createBufferRaw(verts.size() * 12 + 16, 1|2);
        idxBuf_ = s_->createBufferRaw(indices.size() * 4 + 16, 4|2);
        s_->writeBufferRaw(vertBuf_, 0, verts.data(), verts.size() * 12);
        s_->writeBufferRaw(idxBuf_, 0, indices.data(), indices.size() * 4);
        valueMin_ = grid.valueRange.min; valueMax_ = grid.valueRange.max;
        xRange_ = grid.xRange; yRange_ = grid.yRange;
    }
    void draw(Cmd& cmd, Rect2D rect, const Camera3D& camera, bool shade,
              float lightAzdeg, float lightAltdeg) const override {
        if (!indexCount_) return;
        PDrawSurface p{};
        p.clip = clipF(rect);
        const auto vp = camera.viewProjection();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                p.vp[j * 4 + i] = vp[i * 4 + j];   // row → column major
        p.gridRange[0] = xRange_.min; p.gridRange[1] = xRange_.max;
        p.gridRange[2] = yRange_.min; p.gridRange[3] = yRange_.max;
        const float az = lightAzdeg * 3.14159265f / 180.0f;
        const float al = lightAltdeg * 3.14159265f / 180.0f;
        p.light[0] = std::cos(al) * std::cos(az);
        p.light[1] = std::cos(al) * std::sin(az);
        p.light[2] = std::sin(al);
        p.light[3] = shade ? 1.0f : 0.0f;
        p.valueMin = valueMin_; p.valueMax = valueMax_;
        p.vertBuf = vertBuf_; p.idxBuf = idxBuf_;
        p.indexCount = indexCount_;
        ops(cmd).emit(Op::DrawSurface, p);
    }
private:
    OpGpuServices* s_;
    uint32_t vertBuf_ = 0, idxBuf_ = 0, indexCount_ = 0;
    float valueMin_ = 0, valueMax_ = 1;
    Range xRange_{0, 1}, yRange_{0, 1};
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
        const uint32_t nSeg = uint32_t(px.size() - 1);
        uint32_t inBuf = s_->createBufferRaw(px.size_bytes() + 16, 1|2);
        s_->writeBufferRaw(inBuf, 0, px.data(), px.size_bytes());

        // Dashes: the pattern runs continuously along the polyline, so
        // the shader needs each point's cumulative arc length. Computing
        // it here is a cheap O(n) scalar pass — the expensive part (the
        // per-segment stroking) is what moves to the device.
        uint32_t lenBuf = 0, dashBuf = 0, dashCount = 0, dashMul = 1;
        float dashOffset = sp.dashOffset;
        if (!sp.dashes.empty() &&
            render::OffloadConfig::allowGpu(
                render::OffloadConfig::global().dashes)) {
            std::vector<float> cum(px.size(), 0.0f);
            float maxSeg = 0.0f;
            for (size_t i = 1; i < px.size(); ++i) {
                const float dx = px[i].x - px[i - 1].x;
                const float dy = px[i].y - px[i - 1].y;
                const float len = std::hypot(dx, dy);
                // Non-finite points break the run; restart the phase.
                cum[i] = std::isfinite(len) ? cum[i - 1] + len
                                            : cum[i - 1];
                if (std::isfinite(len)) maxSeg = std::max(maxSeg, len);
            }
            float period = 0.0f;
            for (float d : sp.dashes) period += std::max(d, 0.0f);
            if (period > 0.0f && maxSeg > 0.0f) {
                dashMul = std::min<uint32_t>(
                    uint32_t(std::ceil(maxSeg / period)) + 1u, 64u);
                lenBuf = s_->createBufferRaw(cum.size() * 4 + 16, 1|2);
                s_->writeBufferRaw(lenBuf, 0, cum.data(),
                                   cum.size() * 4);
                dashBuf = s_->createBufferRaw(
                    sp.dashes.size() * 4 + 16, 1|2);
                s_->writeBufferRaw(dashBuf, 0, sp.dashes.data(),
                                   sp.dashes.size() * 4);
                dashCount = uint32_t(sp.dashes.size());
            }
        }

        // Worst-case tess output: 6 verts/segment (join may add), times
        // the dash slots when dashing. Joins are not stroked on the
        // dashed path but the slots stay allocated for layout parity.
        uint64_t outBytes = uint64_t(px.size()) * 6 * 32 * dashMul + 64;
        uint32_t outBuf = s_->createBufferRaw(outBytes, 1|2);
        PTessLines p{inBuf, 0, outBuf, 0,
                     uint32_t(px.size()), nSeg,
                     sp.width * 0.5f,
                     uint8_t(sp.join), uint8_t(sp.cap), sp.miterLimit,
                     color.r, color.g, color.b, color.a};
        p.lenBuf = lenBuf;
        p.dashBuf = dashBuf;
        p.dashCount = dashCount;
        p.dashMul = dashMul;
        p.dashOffset = dashOffset;
        os.emit(Op::TessLines, p);
        // The interpreter fills in the real vertex count after
        // dispatch; v1 assumes the 6-verts/segment upper bound.
        return {Mesh{GpuBuf(outBuf), 0,
                     uint32_t(px.size() - 1) * 6 * dashMul}};
    }
    bool envelopeColumns(GpuBuf, uint32_t, float, float, int, int,
                         std::vector<float>&,
                         std::vector<float>&) override { return false; }
    bool supportsDashes() const noexcept override { return true; }
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
        // Emit the reduce with a fresh mailbox slot; the JS interpreter
        // computes it via WGSL and delivers through _vp_mailbox next
        // frame (§6). Meanwhile return the last delivered result for
        // this buffer — one frame of latency under streaming data, no
        // synchronous GPU→CPU stall.
        if (s_->curStream() && count) {
            uint32_t slot = s_->allocMailbox();
            s_->trackReduceSlot(slot, uint32_t(buf));
            PReduceMinMax p{uint32_t(buf), count, slot};
            s_->curStream()->emit(Op::ReduceMinMax, p);
        }
        if (auto* r = s_->reduceResult(uint32_t(buf))) return *r;
        return std::nullopt;   // first frame: caller falls back to CPU
    }
    bool ready() const noexcept override { return true; }
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
                      ->createBufferRaw(uint64_t(count) * 8 + 16, 1|2));
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

// ═══ OpKdeEvalRenderer (async: emit KdeEval2D, deliver next frame) ═══
//
// The interface is synchronous; the op stream can't read back in the
// same frame. Strategy: emit the compute + bulk mailbox, return {}
// (callers use their CPU path); once the mailbox lands, subsequent
// eval() calls with identical params+sample fingerprint get the GPU
// result. Params/fingerprint changes re-emit.

class OpKdeEvalRenderer final : public pr::KdeEvalRenderer {
public:
    explicit OpKdeEvalRenderer(OpGpuServices& s) : s_(&s) {}

    std::vector<float> eval(const std::vector<Point2D>& samples,
                            uint32_t gridW, uint32_t gridH,
                            float xMin, float xMax,
                            float yMin, float yMax,
                            float bwX, float bwY) override {
        if (samples.empty() || !gridW || !gridH) return {};
        // Cheap content fingerprint: endpoints + middle sample.
        const auto& a = samples.front(), &b = samples.back(),
                  &m = samples[samples.size() / 2];
        const bool same =
            pending_ && n_ == samples.size() && gw_ == gridW &&
            gh_ == gridH && x0_ == xMin && x1_ == xMax &&
            y0_ == yMin && y1_ == yMax && bwX_ == bwX && bwY_ == bwY &&
            fp_ == (a.x + a.y + m.x + m.y + b.x + b.y);
        if (same) {
            if (!cached_.empty()) return cached_;
            if (s_->mailboxReady(slot_)) {
                auto bytes = s_->mailboxTake(slot_);
                cached_.resize(bytes.size() / 4);
                std::memcpy(cached_.data(), bytes.data(),
                            cached_.size() * 4);
                return cached_;
            }
            return {};      // GPU readback in flight — CPU covers
        }
        // New/changed request: upload samples, emit KdeEval2D.
        pending_ = true;
        n_ = uint32_t(samples.size()); gw_ = gridW; gh_ = gridH;
        x0_ = xMin; x1_ = xMax; y0_ = yMin; y1_ = yMax;
        bwX_ = bwX; bwY_ = bwY;
        fp_ = a.x + a.y + m.x + m.y + b.x + b.y;
        cached_.clear();
        slot_ = s_->allocMailbox();

        const uint32_t cells = gridW * gridH;
        uint32_t in = s_->createBufferRaw(samples.size() * 8 + 16, 1 | 2);
        s_->writeBufferRaw(in, 0, samples.data(), samples.size() * 8);
        uint32_t out = s_->createBufferRaw(size_t(cells) * 4 + 16, 2 | 16);
        constexpr float kTwoPi = 6.28318530718f;
        PKdeEval2D p{
            .inBuf = in, .n = n_, .outBuf = out,
            .gridW = gridW, .gridH = gridH,
            .xMin = xMin, .xStep = (xMax - xMin) / float(gridW),
            .yMin = yMin, .yStep = (yMax - yMin) / float(gridH),
            .inv2bwX2 = 1.0f / (2.0f * bwX * bwX),
            .inv2bwY2 = 1.0f / (2.0f * bwY * bwY),
            .norm = 1.0f / (kTwoPi * bwX * bwY * float(samples.size())),
            .mailbox = slot_,
        };
        s_->curStream()->emit(Op::KdeEval2D, p);
        return {};
    }

    bool ready() const noexcept override { return true; }
private:
    OpGpuServices* s_;
    bool pending_ = false;
    uint32_t n_ = 0, gw_ = 0, gh_ = 0, slot_ = 0;
    float x0_ = 0, x1_ = 0, y0_ = 0, y1_ = 0, bwX_ = 0, bwY_ = 0, fp_ = 0;
    std::vector<float> cached_;
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
