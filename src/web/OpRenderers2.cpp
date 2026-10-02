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
        const float kTwoPi = 6.28318530718f;
        float total = 0;
        for (float v : data.values) total += std::fabs(v);
        if (total <= 0) return;
        float a0 = 0.0f;
        const float r1 = data.donut ? std::max(data.innerRadius, 0.f)
                                    : 0.f;
        for (size_t i = 0; i < data.values.size(); ++i) {
            float frac = std::fabs(data.values[i]) / total;
            float a1 = a0 + frac * kTwoPi;
            Color c = i < data.colors.size() ? data.colors[i] : Color{};
            int segs = std::max(2, int(frac * 128.0f));
            for (int k = 0; k < segs; ++k) {
                float t0 = a0 + (a1 - a0) * (float(k) / segs);
                float t1 = a0 + (a1 - a0) * (float(k + 1) / segs);
                Point2D p0{std::cos(t0), std::sin(t0)};
                Point2D p1{std::cos(t1), std::sin(t1)};
                if (r1 <= 0) {
                    Point2D tri[3] = {{0, 0}, p0, p1};
                    for (auto v : tri) { verts_.push_back(v);
                                         cols_.push_back(c); }
                } else {
                    Point2D i0{r1 * p0.x, r1 * p0.y};
                    Point2D i1{r1 * p1.x, r1 * p1.y};
                    Point2D quad[6] = {i0, p0, p1, i0, p1, i1};
                    for (auto v : quad) { verts_.push_back(v);
                                          cols_.push_back(c); }
                }
            }
            a0 = a1;
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
