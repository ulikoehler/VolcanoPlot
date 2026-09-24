// volcano/plot/plots/QuiverKeyPlot.cpp
#include "volcano/plot/plots/QuiverKeyPlot.hpp"
#include "volcano/plot/plots/QuiverPlot.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <cmath>

namespace volcano::plot {

QuiverKeyPlot::QuiverKeyPlot(float x, float y, float u,
                             const QuiverPlot* ref, Config cfg)
    : x_(x), y_(y), u_(u), ref_(ref), cfg_(std::move(cfg)) {
    zorder = 0.1f;  // mpl: Q.zorder + 0.1 (quiver zorder is 0)
}

Point2D QuiverKeyPlot::anchorPx(Rect2D rect) const {
    // axes fraction (Y-up) → canvas pixel (Y-down).
    return {rect.x + x_ * float(rect.width),
            rect.y + (1.0f - y_) * float(rect.height)};
}

QuiverKeyPlot::Geom QuiverKeyPlot::geometry(const Axes& axes,
                                            Rect2D rect) const {
    // mpl: the key arrow is built with Q's own scaling — a magnitude-U
    // vector rendered exactly like a field arrow of magnitude U.
    float dataLen = u_;
    if (ref_) {
        float span = std::fabs(axes.viewport().x.span());
        dataLen = u_ * ref_->effectiveScale(axes);
        if (span > 0.0f) dataLen = dataLen / span * 1.0f;  // data units
    }
    // Data-x displacement → pixels along the x axis.
    float spanX = std::fabs(axes.viewport().x.span());
    float lenPx = spanX > 0.0f
        ? dataLen / spanX * float(rect.width) : dataLen;
    Geom g;
    g.shaftW = ref_ ? ref_->shaftWidthPx(float(rect.width)) : 1.5f;
    g.shaftW = std::max(g.shaftW, 0.75f);
    // mpl head: headlength 5×, headwidth 3× shaft width.
    g.headLen = std::max(5.0f * g.shaftW, 3.0f);
    g.headW = std::max(3.0f * g.shaftW, 2.0f);

    Point2D a = anchorPx(rect);
    float rad = cfg_.angleDeg * float(M_PI) / 180.0f;
    // mpl angle is CCW from +x in data space → -sin in Y-down pixels.
    Point2D dir{std::cos(rad), -std::sin(rad)};
    // mpl pivot per labelpos: N/S → middle, E → tip, W → tail.
    float pivot = (cfg_.labelPos == LabelPos::E) ? 1.0f
                : (cfg_.labelPos == LabelPos::W) ? 0.0f : 0.5f;
    g.tail = {a.x - dir.x * lenPx * pivot, a.y - dir.y * lenPx * pivot};
    g.tip = {g.tail.x + dir.x * lenPx, g.tail.y + dir.y * lenPx};
    return g;
}

void QuiverKeyPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                         const Axes& axes, Rect2D rect) {
    auto ext = r.backend().extent();
    vk::Rect2D full{vk::Offset2D{0, 0}, ext};
    Geom g = geometry(axes, rect);
    Color col = cfg_.color.a > 0.0f ? cfg_.color
                : (ref_ ? ref_->legendColor() : Color::black());

    // Shaft: line tail → shaft end (tip minus head).
    float dx = g.tip.x - g.tail.x, dy = g.tip.y - g.tail.y;
    float len = std::hypot(dx, dy);
    if (len <= 0.0f) return;
    Point2D dir{dx / len, dy / len}, perp{-dir.y, dir.x};
    Point2D shaftEnd{g.tip.x - dir.x * std::min(g.headLen, len * 0.9f),
                     g.tip.y - dir.y * std::min(g.headLen, len * 0.9f)};
    Point2D shaft[2] = {g.tail, shaftEnd};
    r.spineRenderer().drawLineStrip(cmd, full, ext,
                                    std::span{shaft, 2}, col, g.shaftW);

    // mpl notched arrowhead: tip, back corner, axis notch, back corner.
    float hl = std::min(g.headLen, len);
    float hw2 = g.headW * 0.5f;
    float hal = g.headLen * 0.9f;  // headaxislength ≈ 0.9×headlength
    Point2D backL{g.tip.x - dir.x * hl + perp.x * hw2,
                  g.tip.y - dir.y * hl + perp.y * hw2};
    Point2D backR{g.tip.x - dir.x * hl - perp.x * hw2,
                  g.tip.y - dir.y * hl - perp.y * hw2};
    Point2D notch{g.tip.x - dir.x * std::max(hl - hal * 0.5f, 0.0f),
                  g.tip.y - dir.y * std::max(hl - hal * 0.5f, 0.0f)};
    Point2D tri[6] = {g.tip, backR, notch, g.tip, notch, backL};
    r.spineRenderer().drawTriangles(cmd, full, ext,
                                    std::span{tri, 6}, col);

    // Label.
    if (!cfg_.label.empty()) {
        auto m = r.measureRichText(cfg_.label, 1.0f);
        float sep = cfg_.labelSepPx;
        Point2D a = anchorPx(rect);
        float lx = a.x, ly = a.y;
        Color lc = cfg_.labelColor.a > 0.0f ? cfg_.labelColor
                                           : axes.style().textColor;
        switch (cfg_.labelPos) {
        case LabelPos::N:
            lx = a.x - m.width * 0.5f;
            ly = a.y - g.shaftW * 0.5f - sep;
            break;
        case LabelPos::S:
            lx = a.x - m.width * 0.5f;
            ly = a.y + g.shaftW * 0.5f + sep + m.ascent;
            break;
        case LabelPos::E:
            lx = a.x + sep;
            ly = a.y - m.height * 0.5f + m.ascent;
            break;
        case LabelPos::W:
            lx = a.x - sep - m.width;
            ly = a.y - m.height * 0.5f + m.ascent;
            break;
        }
        r.drawRichText(cmd, full, cfg_.label, lx, ly, lc, 1.0f);
    }
}

void QuiverKeyPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                               Rect2D rect) {
    Geom g = geometry(axes, rect);
    Color col = cfg_.color.a > 0.0f ? cfg_.color
                : (ref_ ? ref_->legendColor() : Color::black());
    float dx = g.tip.x - g.tail.x, dy = g.tip.y - g.tail.y;
    float len = std::hypot(dx, dy);
    if (len <= 0.0f) return;
    Point2D dir{dx / len, dy / len}, perp{-dir.y, dir.x};
    Point2D shaftEnd{g.tip.x - dir.x * std::min(g.headLen, len * 0.9f),
                     g.tip.y - dir.y * std::min(g.headLen, len * 0.9f)};
    render::VectorCanvas::Pen pen;
    pen.color = col;
    pen.width = g.shaftW;
    Point2D shaft[2] = {g.tail, shaftEnd};
    c.polyline(shaft, pen);
    float hl = std::min(g.headLen, len);
    float hw2 = g.headW * 0.5f;
    Point2D backL{g.tip.x - dir.x * hl + perp.x * hw2,
                  g.tip.y - dir.y * hl + perp.y * hw2};
    Point2D backR{g.tip.x - dir.x * hl - perp.x * hw2,
                  g.tip.y - dir.y * hl - perp.y * hw2};
    Point2D notch{g.tip.x - dir.x * hl * 0.55f,
                  g.tip.y - dir.y * hl * 0.55f};
    Point2D head[4] = {g.tip, backR, notch, backL};
    c.polygon(head, col);
    if (!cfg_.label.empty()) {
        Point2D a = anchorPx(rect);
        float sep = cfg_.labelSepPx;
        float fs = axes.style().fontSize;
        Color lc = cfg_.labelColor.a > 0.0f ? cfg_.labelColor
                                           : axes.style().textColor;
        float lx = a.x, ly = a.y;
        // Vector canvas text positions the baseline's left edge.
        switch (cfg_.labelPos) {
        case LabelPos::N: ly = a.y - g.shaftW * 0.5f - sep; break;
        case LabelPos::S: ly = a.y + g.shaftW * 0.5f + sep + fs; break;
        case LabelPos::E: lx = a.x + sep; ly = a.y + fs * 0.35f; break;
        case LabelPos::W: lx = a.x - sep; ly = a.y + fs * 0.35f; break;
        }
        c.text({lx, ly}, cfg_.label, fs, lc);
    }
}

} // namespace volcano::plot
