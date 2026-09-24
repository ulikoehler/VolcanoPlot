// volcano/plot/plots/BarPlot.cpp
#include "volcano/plot/plots/BarPlot.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/backend/Backend.hpp"
#include "../VectorEmitHelpers.hpp"
namespace volcano::plot {
void BarPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void BarPlot::draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect = clipRectVk(rect, r.backend().extent());
    // mpl patheffects: replay each bar per effect pass on the CPU
    // (the GPU instanced path can't offset/override per pass).
    if (!pathEffects.empty()) {
        auto& spine = r.spineRenderer();
        vk::Extent2D res = r.backend().extent();
        const float dpi = axes.style().dpi;
        auto toPx = [&](Point2D p) {
            auto f = axes.dataToFraction(p);
            return Point2D{rect.x + f.x * float(rect.width),
                           rect.y + rect.height - f.y * float(rect.height)};
        };
        auto barQuad = [&](size_t i, Point2D off) {
            float h = data_.heights[i];
            float bw = data_.width;
            Point2D a, b;
            if (data_.horizontal) {
                float y0 = float(i) - bw * 0.5f;
                a = toPx({0.0f, y0 + bw}); b = toPx({h, y0});
            } else {
                float x0 = float(i) - bw * 0.5f;
                a = toPx({x0, 0.0f}); b = toPx({x0 + bw, h});
            }
            return std::vector<Point2D>{
                {a.x + off.x, a.y + off.y}, {b.x + off.x, a.y + off.y},
                {b.x + off.x, b.y + off.y},
                {a.x + off.x, a.y + off.y}, {b.x + off.x, b.y + off.y},
                {a.x + off.x, b.y + off.y}};
        };
        for (size_t i = 0; i < data_.heights.size(); ++i) {
            Color col = i < data_.colors.size() ? data_.colors[i]
                        : Color::fromRgba8(31, 119, 180);
            auto quad = [&](Point2D off, Color c) {
                if (c.a <= 0) return;
                spine.drawTriangles(cmd, vrect, res, barQuad(i, off), c);
            };
            for (const auto& e : pathEffects) {
                Point2D off = e.offsetPx(dpi);
                switch (e.kind) {
                case PathEffect::Kind::Normal:
                    quad({0.0f, 0.0f}, col); break;
                case PathEffect::Kind::PatchShadow:
                case PathEffect::Kind::LineShadow: {
                    // mpl: fill-only copy, base*rho or explicit color.
                    Color base = col;
                    Color s = e.shadowColor.value_or(Color{
                        base.r * e.rho, base.g * e.rho,
                        base.b * e.rho, 1.0f});
                    s.a = e.shadowAlpha;
                    quad(off, s); break;
                }
                case PathEffect::Kind::Stroke: {
                    // mpl Stroke on a patch: normal fill + edge stroked
                    // in the gc foreground at the gc linewidth.
                    quad(off, col);
                    Color ec = e.foreground.value_or(col);
                    float elw = e.strokeWidthPx(2.0f, dpi);
                    if (ec.a > 0) {
                        auto q = barQuad(i, off);
                        std::array<Point2D, 5> ring{
                            q[0], q[1], q[2], q[5], q[0]};
                        auto mesh = strokePolyline(ring,
                            StrokeParams{.width = elw,
                                         .join = JoinStyle::Miter});
                        if (!mesh.verts.empty())
                            spine.drawTriangles(cmd, vrect, res,
                                                mesh.verts, ec);
                    }
                    break;
                }
                }
                if (e.thenNormal) quad({0.0f, 0.0f}, col);
            }
        }
        return;
    }
    Transform2D t = axes.transform();
    renderer_.draw(cmd, vrect, t);
}
void BarPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    if (data_.heights.empty()) return;
    float bw = data_.width;
    auto toPx = pxMapper(axes, rect);
    for (size_t i = 0; i < data_.heights.size(); ++i) {
        float h = data_.heights[i];
        Point2D a, b;
        if (data_.horizontal) {
            float y0 = float(i) - data_.width * 0.5f;
            a = toPx({0.0f, y0 + bw}); b = toPx({h, y0});
        } else {
            float x0 = float(i) - data_.width * 0.5f;
            a = toPx({x0, 0.0f}); b = toPx({x0 + bw, h});
        }
        Color col = i < data_.colors.size() ? data_.colors[i]
                                            : Color::fromRgba8(31,119,180);
        Point2D q[4] = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
        const float dpi = axes.style().dpi;
        auto pass = [&](Point2D off, Color fc, Color ec, float elw) {
            Point2D s[4] = {{q[0].x + off.x, q[0].y + off.y},
                            {q[1].x + off.x, q[1].y + off.y},
                            {q[2].x + off.x, q[2].y + off.y},
                            {q[3].x + off.x, q[3].y + off.y}};
            if (fc.a > 0) c.polygon(s, fc);
            if (ec.a > 0 && elw > 0) {
                render::VectorCanvas::Pen pen;
                pen.color = ec; pen.width = elw;
                std::vector<Point2D> ring{s[0], s[1], s[2], s[3], s[0]};
                c.polyline(ring, pen);
            }
        };
        if (pathEffects.empty()) { pass({0, 0}, col, {}, 0); continue; }
        for (const auto& e : pathEffects) {
            Point2D off = e.offsetPx(dpi);
            switch (e.kind) {
            case PathEffect::Kind::Normal:
                pass({0, 0}, col, {}, 0); break;
            case PathEffect::Kind::Stroke:
                pass(off, col, e.foreground.value_or(col),
                     e.strokeWidthPx(2.0f, dpi));
                break;
            case PathEffect::Kind::PatchShadow:
            case PathEffect::Kind::LineShadow: {
                Color s = e.shadowColor.value_or(Color{
                    col.r * e.rho, col.g * e.rho, col.b * e.rho, 1.0f});
                s.a = e.shadowAlpha;
                pass(off, s, {}, 0); break;
            }
            }
            if (e.thenNormal) pass({0, 0}, col, {}, 0);
        }
    }
}
void BarPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, -data_.width * 0.5f);
    v.x.max = std::max(v.x.max,
                       float(data_.heights.size()) - 1.0f +
                           data_.width * 0.5f);
    v.y.min = std::min(v.y.min, 0.0f);
    for (float h : data_.heights) v.y.max = std::max(v.y.max, h);
}
} // namespace volcano::plot
