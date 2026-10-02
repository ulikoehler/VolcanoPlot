// src/web/OpRenderers.cpp — op-recording implementations of the
// backend-neutral primitive interfaces (WEBGPU-PLAN §3).
//
// Each Op* impl holds an OpGpuServices& for handle allocation and
// records ops into the Cmd's OpStream. No GPU work happens here — the
// TS interpreter replays the stream.
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


namespace volcano::web {

using render::Cmd;
using render::GpuBuf;
using render::GpuTex;
using namespace plot;

// ═══ helpers ═════════════════════════════════════════════════════════

namespace {
[[nodiscard]] inline Rect2Df clipF(Rect2D r) { return toF(r); }
} // namespace

// ═══ OpSpineRenderer ═════════════════════════════════════════════════

class OpSpineRenderer final : public render::primitives::SpineRenderer {
public:
    explicit OpSpineRenderer(OpGpuServices& s) : s_(&s) {}

    void drawRect(Cmd& cmd, Rect2D clip, Extent2D res,
                  Rect2D rect, Color color, float lineWidth) override {
        (void)res;
        const float x = float(rect.x), y = float(rect.y);
        const float w = float(rect.width), h = float(rect.height);
        Point2D pts[5] = {{x, y}, {x + w, y}, {x + w, y + h},
                          {x, y + h}, {x, y}};
        PDrawLinePx p{clipF(clip),
                      ops(cmd).arenaCopy(std::span<const Point2D>{pts, 5}),
                      color.r, color.g, color.b, color.a,
                      std::max(lineWidth, 1.0f)};
        ops(cmd).emit(Op::DrawLineStripPx, p);
    }
    void drawFilledRect(Cmd& cmd, Rect2D clip, Extent2D res,
                        Rect2D rect, Color color) override {
        (void)res;
        const float x = float(rect.x), y = float(rect.y);
        const float r = x + float(rect.width), b = y + float(rect.height);
        Point2D tris[6] = {{x, y}, {r, y}, {r, b},
                           {x, y}, {r, b}, {x, b}};
        PDrawTrisPx p{clipF(clip),
                      ops(cmd).arenaCopy(std::span<const Point2D>{tris, 6}),
                      color.r, color.g, color.b, color.a};
        ops(cmd).emit(Op::DrawTrisPx, p);
    }
    void drawTicks(Cmd& cmd, Rect2D clip, Extent2D res,
                   Rect2D rect, std::span<const float> positions,
                   Color color, float tickLength, bool yAxis,
                   float dataMin, float dataMax, float inFrac,
                   bool farSide, float tickWidth) override {
        (void)res;
        std::vector<Point2D> segs;
        segs.reserve(positions.size() * 2);
        const float in = tickLength * inFrac;
        const float out = tickLength - in;
        for (float pos : positions) {
            if (pos < dataMin || pos > dataMax) continue;
            if (!yAxis) {
                float px = rect.x + (pos - dataMin)
                         / (dataMax - dataMin) * float(rect.width);
                float yBase = farSide ? float(rect.y)
                                      : float(rect.y + rect.height);
                float dir = farSide ? 1.0f : -1.0f;
                segs.push_back({px, yBase + dir * out});
                segs.push_back({px, yBase - dir * in});
            } else {
                float py = rect.y + float(rect.height)
                         - (pos - dataMin)
                         / (dataMax - dataMin) * float(rect.height);
                float xBase = farSide ? float(rect.x + rect.width)
                                      : float(rect.x);
                float dir = farSide ? -1.0f : 1.0f;
                segs.push_back({xBase - dir * out, py});
                segs.push_back({xBase + dir * in, py});
            }
        }
        if (segs.empty()) return;
        PDrawLinePx p{clipF(clip),
                      ops(cmd).arenaCopy(std::span<const Point2D>{segs}),
                      color.r, color.g, color.b, color.a,
                      std::max(tickWidth, 1.0f)};
        ops(cmd).emit(Op::DrawSegmentsPx, p);
    }
    void drawLineStrip(Cmd& cmd, Rect2D clip, Extent2D res,
                       std::span<const Point2D> points,
                       Color color, float width) override {
        (void)res;
        if (points.size() < 2) return;
        PDrawLinePx p{clipF(clip), ops(cmd).arenaCopy(points),
                      color.r, color.g, color.b, color.a,
                      std::max(width, 1.0f)};
        ops(cmd).emit(Op::DrawLineStripPx, p);
    }
    void drawTriangles(Cmd& cmd, Rect2D clip, Extent2D res,
                       std::span<const Point2D> triVerts,
                       Color color) override {
        (void)res;
        if (triVerts.empty()) return;
        PDrawTrisPx p{clipF(clip), ops(cmd).arenaCopy(triVerts),
                      color.r, color.g, color.b, color.a};
        ops(cmd).emit(Op::DrawTrisPx, p);
    }
    void drawTrianglesVC(Cmd& cmd, Rect2D clip, Extent2D res,
                         std::span<const Point2D> triVerts,
                         std::span<const Color> colors) override {
        (void)res;
        if (triVerts.empty()) return;
        PDrawTrisPxVC p{clipF(clip), ops(cmd).arenaCopy(triVerts),
                        ops(cmd).arenaCopy(colors)};
        ops(cmd).emit(Op::DrawTrisPxVC, p);
    }
    void drawTrianglesGpu(Cmd& cmd, Rect2D clip, Extent2D res,
                          GpuBuf buffer, uint64_t byteOffset,
                          uint32_t vertexCount) override {
        PDrawTrisGpu p{clipF(clip), float(res.width), float(res.height),
                       uint32_t(buffer), byteOffset, vertexCount};
        ops(cmd).emit(Op::DrawTrisGpu, p);
    }
    void resetScratch() override {}
private:
    OpGpuServices* s_;
};

// ═══ OpLineRenderer / OpLineSegmentRenderer ══════════════════════════

class OpLineRenderer final : public render::primitives::LineRenderer {
public:
    explicit OpLineRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(std::span<const Point2D> points, Color color,
                float width) override {
        color_ = color;
        width_ = width;
        updatePoints(points);
    }
    void updatePoints(std::span<const Point2D> points) override {
        if (points.size() > capacity_) {
            if (buf_) s_->releaseBuffer(buf_);
            buf_ = s_->createBufferRaw(points.size_bytes() + 16,
                                       /*vertex|storage*/ 1|2);
            capacity_ = uint32_t(points.size());
        }
        s_->writeBufferRaw(buf_, 0, points.data(), points.size_bytes());
        count_ = uint32_t(points.size());
    }
    void bindExternalBuffer(GpuBuf buf, uint32_t count) override {
        buf_ = uint32_t(buf); count_ = count;
        capacity_ = count;
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t,
              uint32_t pointCount) const override {
        if (!buf_ || pointCount < 2) return;
        PDrawLines p{clipF(rect), clipF(rect),
                     makeTransformUBO(t, rect, color_),
                     buf_, pointCount,
                     color_.r, color_.g, color_.b, color_.a, width_};
        ops(cmd).emit(Op::DrawLines, p);
    }
    GpuBuf pointBuffer() const noexcept override { return buf_; }
    uint32_t pointCount() const noexcept override { return count_; }
private:
    OpGpuServices* s_;
    uint32_t buf_ = 0, count_ = 0, capacity_ = 0;
    Color color_{};
    float width_ = 1.0f;
};

class OpLineSegmentRenderer final
    : public render::primitives::LineSegmentRenderer {
public:
    explicit OpLineSegmentRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(std::span<const Point2D> points, Color color,
                float width) override {
        color_ = color; width_ = width;
        if (buf_) s_->releaseBuffer(buf_);
        buf_ = s_->createBufferRaw(points.size_bytes() + 16, 1|2);
        s_->writeBufferRaw(buf_, 0, points.data(), points.size_bytes());
        count_ = uint32_t(points.size());
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t,
              uint32_t vertexCount) const override {
        if (!buf_ || vertexCount < 2) return;
        PDrawLines p{clipF(rect), clipF(rect),
                     makeTransformUBO(t, rect, color_),
                     buf_, vertexCount,
                     color_.r, color_.g, color_.b, color_.a, width_};
        ops(cmd).emit(Op::DrawLineSegs, p);
    }
    GpuBuf pointBuffer() const noexcept override { return buf_; }
    uint32_t pointCount() const noexcept override { return count_; }
private:
    OpGpuServices* s_;
    uint32_t buf_ = 0, count_ = 0;
    Color color_{};
    float width_ = 1.0f;
};

// ═══ OpPointRenderer ═════════════════════════════════════════════════

class OpPointRenderer final : public render::primitives::PointRenderer {
public:
    explicit OpPointRenderer(OpGpuServices& s) : s_(&s) {}

    void upload(std::span<const Point2D> points,
                std::span<const Color> colors,
                std::span<const float> sizes) override {
        count_ = uint32_t(points.size());
        ensureBufs(count_);
        hasCol_ = !colors.empty(); hasSize_ = !sizes.empty();
        s_->writeBufferRaw(posBuf_, 0, points.data(), points.size_bytes());
        if (!colors.empty())
            s_->writeBufferRaw(colBuf_, 0, colors.data(),
                               colors.size_bytes());
        if (!sizes.empty())
            s_->writeBufferRaw(sizeBuf_, 0, sizes.data(),
                               sizes.size_bytes());
    }
    void updatePoints(std::span<const Point2D> points,
                      std::span<const Color> colors,
                      std::span<const float> sizes) override {
        if (points.size() > capacity_) { upload(points, colors, sizes); return; }
        count_ = uint32_t(points.size());
        hasCol_ = !colors.empty(); hasSize_ = !sizes.empty();
        s_->writeBufferRaw(posBuf_, 0, points.data(), points.size_bytes());
        if (!colors.empty())
            s_->writeBufferRaw(colBuf_, 0, colors.data(),
                               colors.size_bytes());
        if (!sizes.empty())
            s_->writeBufferRaw(sizeBuf_, 0, sizes.data(),
                               sizes.size_bytes());
    }
    void draw(Cmd& cmd, Rect2D rect, const Transform2D& t,
              uint32_t count,
              render::primitives::MarkerParams marker) const override {
        draw(cmd, rect, rect, t, count, marker);
    }
    void draw(Cmd& cmd, Rect2D viewport, Rect2D scissor,
              const Transform2D& t, uint32_t count,
              render::primitives::MarkerParams marker) const override {
        if (!posBuf_ || count == 0) return;
        PDrawPoints p{clipF(scissor), clipF(viewport),
                      makeTransformUBO(t, viewport, {}),
                      posBuf_, colBuf_, sizeBuf_, count,
                      (hasCol_ ? 1u : 0u) | (hasSize_ ? 2u : 0u),
                      {marker.code, marker.fill, marker.numsides,
                       marker.angle}};
        ops(cmd).emit(Op::DrawPoints, p);
    }
    GpuBuf pointBuffer() const noexcept override { return posBuf_; }
    uint32_t pointCount() const noexcept override { return count_; }
    bool hasData() const noexcept override { return posBuf_ != 0; }
    void resetScratch() override {}
private:
    void ensureBufs(uint32_t n) {
        if (n <= capacity_) return;
        if (posBuf_) { s_->releaseBuffer(posBuf_);
                       s_->releaseBuffer(colBuf_);
                       s_->releaseBuffer(sizeBuf_); }
        posBuf_ = s_->createBufferRaw(uint64_t(n) * 8 + 16, 1|2);
        colBuf_ = s_->createBufferRaw(uint64_t(n) * 16 + 16, 1|2);
        sizeBuf_ = s_->createBufferRaw(uint64_t(n) * 4 + 16, 1|2);
        capacity_ = n;
    }
    OpGpuServices* s_;
    uint32_t posBuf_ = 0, colBuf_ = 0, sizeBuf_ = 0;
    uint32_t count_ = 0, capacity_ = 0;
    bool hasCol_ = false, hasSize_ = false;
};

namespace op {
std::unique_ptr<render::primitives::SpineRenderer>
    makeSpine(OpGpuServices& s) {
    return std::make_unique<OpSpineRenderer>(s);
}
std::unique_ptr<render::primitives::PointRenderer>
    makePoint(OpGpuServices& s) {
    return std::make_unique<OpPointRenderer>(s);
}
std::unique_ptr<render::primitives::LineRenderer>
    makeLine(OpGpuServices& s) {
    return std::make_unique<OpLineRenderer>(s);
}
std::unique_ptr<render::primitives::LineSegmentRenderer>
    makeLineSegment(OpGpuServices& s) {
    return std::make_unique<OpLineSegmentRenderer>(s);
}
} // namespace op

} // namespace volcano::web
