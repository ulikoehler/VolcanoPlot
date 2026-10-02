// src/web/OpRenderers2.cpp — op-recording impls, part 2
// (fills, bars, pie, heatmap, surface, instanced, gpuline, reduce,
//  eval, kde, grid3d). See OpRenderers.cpp for spine/line/point.
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

#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Colormap.hpp>

namespace volcano::web {

using render::Cmd;
using render::GpuBuf;
using namespace plot;
namespace pr = render::primitives;

namespace {
[[nodiscard]] inline Rect2Df clipF(Rect2D r) { return toF(r); }
} // namespace

// ═══ OpFillRenderer ══════════════════════════════════════════════════

class OpFillRenderer final : public pr::FillRenderer {
public:
    explicit OpFillRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(std::span<const Point2D> positions,
                std::span<const Color> colors) override {
        count_ = uint32_t(positions.size());
        if (posBuf_) s_->releaseBuffer(posBuf_);
        if (colBuf_) s_->releaseBuffer(colBuf_);
        posBuf_ = s_->createBufferRaw(positions.size_bytes() + 16, 1|2);
        s_->writeBufferRaw(posBuf_, 0, positions.data(),
                           positions.size_bytes());
        colBuf_ = colors.empty() ? 0
            : s_->createBufferRaw(colors.size_bytes() + 16, 1|2);
        if (colBuf_)
            s_->writeBufferRaw(colBuf_, 0, colors.data(),
                               colors.size_bytes());
    }
    void adoptBuffers(GpuBuf positions, GpuBuf colors,
                      uint32_t count) override {
        posBuf_ = uint32_t(positions);
        colBuf_ = uint32_t(colors);
        count_ = count;
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t) const override {
        if (!posBuf_ || count_ == 0) return;
        // Uniform color is unused when colBuf_ != 0 (per-vertex colors).
        PDrawTrisData p{clipF(rect), makeTransformUBO(t, rect, {}),
                        posBuf_, colBuf_, count_, 1, 1, 1, 1};
        ops(cmd).emit(Op::DrawTrisData, p);
    }
    GpuBuf pointBuffer() const noexcept override { return posBuf_; }
    uint32_t pointCount() const noexcept override { return count_; }
private:
    OpGpuServices* s_;
    uint32_t posBuf_ = 0, colBuf_ = 0, count_ = 0;
};

// ═══ OpBarRenderer ═══════════════════════════════════════════════════
// Bars are quads → triangle soup with per-vertex colors (DrawTrisData).

class OpBarRenderer final : public pr::BarRenderer {
public:
    explicit OpBarRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(const BarData& data) override {
        const size_t n = data.heights.size();
        std::vector<Point2D> pos;
        std::vector<Color> col;
        pos.reserve(n * 6); col.reserve(n * 6);
        // Emit bar quads in bar-local coords (x = bar index space,
        // y = height); the shader maps via TransformUBO. We store
        // (x0,y0,x1,y1) expanded to tris on the CPU — cheap for bars.
        const float w = data.width * 0.5f;
        for (size_t i = 0; i < n; ++i) {
            float cx = float(i);       // bar centers at integer index
            float h = data.heights[i];
            float x0 = cx - w, x1 = cx + w;
            Point2D q[6] = {{x0,0},{x1,0},{x1,h},
                            {x0,0},{x1,h},{x0,h}};
            Color c = i < data.colors.size() ? data.colors[i]
                                             : Color{};
            for (auto v : q) { pos.push_back(v); col.push_back(c); }
        }
        count_ = uint32_t(pos.size());
        if (posBuf_) s_->releaseBuffer(posBuf_);
        if (colBuf_) s_->releaseBuffer(colBuf_);
        posBuf_ = s_->createBufferRaw(pos.size() * 8 + 16, 1|2);
        colBuf_ = s_->createBufferRaw(col.size() * 16 + 16, 1|2);
        s_->writeBufferRaw(posBuf_, 0, pos.data(), pos.size() * 8);
        s_->writeBufferRaw(colBuf_, 0, col.data(), col.size() * 16);
        horizontal_ = data.horizontal;
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t) const override {
        if (!posBuf_ || count_ == 0) return;
        Transform2D td = t;
        if (horizontal_) std::swap(td.scaleX, td.scaleY); // cheap approx;
        // interpreter uses ubo verbatim — horizontal handled in WGSL via
        // swapped axes flag (extra[0]).
        auto ubo = makeTransformUBO(td, rect, {});
        ubo.extra[0] = horizontal_ ? 1.0f : 0.0f;
        PDrawTrisData p{clipF(rect), ubo, posBuf_, colBuf_, count_,
                        1, 1, 1, 1};
        ops(cmd).emit(Op::DrawTrisData, p);
    }
private:
    OpGpuServices* s_;
    uint32_t posBuf_ = 0, colBuf_ = 0, count_ = 0;
    bool horizontal_ = false;
};

// ═══ OpPieRenderer ═══════════════════════════════════════════════════
// Wedges are pre-tessellated on the CPU into triangle soup with
// per-vertex colors — no pie shader needed (identical pixels).

class OpPieRenderer final : public pr::PieRenderer {
public:
    explicit OpPieRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(const PieData& data) override {
        verts_.clear(); cols_.clear();
        float total = 0;
        for (float v : data.values) total += v;
        if (total <= 0) return;
        center_ = data.center;

        // Mirror PieRendererVk::upload: mpl pie() semantics —
        // normalize, startAngle, counterclock, explode, prop cycle.
        const float denom = data.normalize ? total : 1.0f;
        const float dir = data.counterclock ? 1.0f : -1.0f;
        constexpr int kSeg = 32;
        constexpr float PI2 = 6.28318530718f;
        const size_t n = data.values.size();
        std::vector<float> sliceStart(n), sliceEnd(n);
        float theta1 = data.startAngle / 360.0f;
        for (size_t i = 0; i < n; ++i) {
            sliceStart[i] = theta1;
            theta1 += dir * (data.values[i] / denom);
            sliceEnd[i] = theta1;
        }
        auto explodeAt = [&](size_t i) {
            if (data.explode.empty()) return 0.0f;
            if (data.explode.size() == 1) return data.explode[0];
            return i < data.explode.size() ? data.explode[i] : 0.0f;
        };
        for (size_t idx = 0; idx < n; ++idx) {
            float sa0 = PI2 * sliceStart[idx];
            float sa1 = PI2 * sliceEnd[idx];
            float r0 = data.innerRadius * data.radius;
            float r1 = data.radius;
            float mid = (sa0 + sa1) * 0.5f;
            float expl = explodeAt(idx);
            Point2D off{center_.x + expl * std::cos(mid),
                        center_.y + expl * std::sin(mid)};
            Color c = idx < data.colors.size() ? data.colors[idx]
                                               : ColorCycle::at(idx);
            for (int k = 0; k < kSeg; ++k) {
                float ta0 = sa0 + (sa1 - sa0) * k / kSeg;
                float ta1 = sa0 + (sa1 - sa0) * (k + 1) / kSeg;
                Point2D i0{off.x + r0 * std::cos(ta0),
                           off.y + r0 * std::sin(ta0)};
                Point2D i1{off.x + r0 * std::cos(ta1),
                           off.y + r0 * std::sin(ta1)};
                Point2D o0{off.x + r1 * std::cos(ta0),
                           off.y + r1 * std::sin(ta0)};
                Point2D o1{off.x + r1 * std::cos(ta1),
                           off.y + r1 * std::sin(ta1)};
                Point2D quad[6] = {i0, i1, o0, i1, o1, o0};
                for (auto v : quad) { verts_.push_back(v);
                                      cols_.push_back(c); }
            }
        }
        count_ = uint32_t(verts_.size());
        if (posBuf_) s_->releaseBuffer(posBuf_);
        if (colBuf_) s_->releaseBuffer(colBuf_);
        posBuf_ = s_->createBufferRaw(verts_.size() * 8 + 16, 1|2);
        colBuf_ = s_->createBufferRaw(cols_.size() * 16 + 16, 1|2);
        s_->writeBufferRaw(posBuf_, 0, verts_.data(), verts_.size() * 8);
        s_->writeBufferRaw(colBuf_, 0, cols_.data(), cols_.size() * 16);
    }
    // Pie draws in pixel space centered on the rect — encode the rect
    // center/scale in the UBO extras; the interpreter maps NDC directly.
    void draw(Cmd& cmd, Rect2D rect) const override {
        if (!posBuf_ || count_ == 0) return;
        TransformUBO ubo{};
        float cx = rect.x + rect.width * 0.5f;
        float cy = rect.y + rect.height * 0.5f;
        float sc = std::min(rect.width, rect.height) * 0.5f;
        ubo.rectX = cx; ubo.rectY = cy; ubo.rectW = sc; ubo.rectH = sc;
        PDrawTrisGpu p{clipF(rect), float(rect.width), float(rect.height),
                       posBuf_, 0, count_};
        // Reuse DrawTrisGpu semantics is wrong here (no line-vertex
        // layout) — emit DrawTrisData with an identity-ish UBO plus the
        // pie affine in extras: ndc handled interpreter-side.
        ubo.extra[1] = 2.0f;  // marker: pie affine mode
        PDrawTrisData d{clipF(rect), ubo, posBuf_, colBuf_, count_,
                        1, 1, 1, 1};
        ops(cmd).emit(Op::DrawPie, d);
    }
private:
    Point2D center_{};
    OpGpuServices* s_;
    uint32_t posBuf_ = 0, colBuf_ = 0, count_ = 0;
    std::vector<Point2D> verts_;
    std::vector<Color> cols_;
};

namespace op {
std::unique_ptr<render::primitives::FillRenderer>
    makeFill(OpGpuServices& s) {
    return std::make_unique<OpFillRenderer>(s);
}
std::unique_ptr<render::primitives::BarRenderer>
    makeBar(OpGpuServices& s) {
    return std::make_unique<OpBarRenderer>(s);
}
std::unique_ptr<render::primitives::PieRenderer>
    makePie(OpGpuServices& s) {
    return std::make_unique<OpPieRenderer>(s);
}
} // namespace op

} // namespace volcano::web
