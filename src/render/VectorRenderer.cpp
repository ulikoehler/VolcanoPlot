// volcano/render/VectorRenderer.cpp — figure → VectorCanvas driver.
// Mirrors the pass order and layout math of the raster Renderer:
// grid → plots (z-order) → spines/ticks → labels → annotations →
// legend → colorbar.
#include "volcano/render/VectorRenderer.hpp"
#include "volcano/plot/Annotation.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/plot/Stroke.hpp"
#include "TickLayout.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace volcano::render {

using plot::Color;
using plot::Point2D;
using plot::Rect2D;
using Pen = VectorCanvas::Pen;

namespace {


Pen penOf(Color c, float w, std::vector<float> dashes = {}) {
    Pen p;
    p.color = c;
    p.width = w;
    p.dashes = std::move(dashes);
    return p;
}

std::vector<float> gridDashes(std::string_view ls) {
    auto style = plot::lineStyleFromString(ls).value_or(plot::LineStyle::Solid);
    return plot::dashPattern(style, 1.0f);
}

/// Emit a mpl FancyBboxPatch-style text background box: boxStyle
/// outline when set (pad/rounding in units of `mutationPx`), else a
/// plain rect. Face when face.a > 0, edge stroked at `edgeW` when
/// edge.a > 0.
void emitTextBbox(VectorCanvas& c, float x0, float y0, float w, float h,
                  float padPx, Color face, Color edge, float edgeW,
                  const std::optional<plot::BoxStyleSpec>& boxStyle,
                  float mutationPx) {
    if (boxStyle) {
        // boxStylePath expands the rect by pad*mutationSize itself —
        // pass the raw text rect, not a pre-padded one.
        auto bs = *boxStyle;
        bs.mutationSize *= mutationPx;
        auto path = plot::boxStylePath(x0, y0, w, h, bs);
        for (auto& sp : path.toPolylines(24)) {
            if (face.a > 0.0f) c.polygon(sp.points, face);
            if (edge.a > 0.0f) {
                auto ring = sp.points;
                if (sp.closed && !ring.empty())
                    ring.push_back(ring.front());
                c.polyline(ring, penOf(edge, edgeW));
            }
        }
    } else {
        float ax = x0 - padPx, ay = y0 - padPx;
        float aw = w + 2 * padPx, ah = h + 2 * padPx;
        Point2D q[5] = {{ax, ay}, {ax + aw, ay}, {ax + aw, ay + ah},
                        {ax, ay + ah}, {ax, ay}};
        if (face.a > 0.0f) c.polygon(std::span{q}.first<4>(), face);
        if (edge.a > 0.0f) c.polyline(std::span{q}, penOf(edge, edgeW));
    }
}

} // namespace

text::TextMeasure VectorRenderer::measure(std::string_view s, float scale) {
    if (measure_) return measure_(s, scale);
    // Fallback estimate (~DejaVu Sans proportions at the 16px reference).
    float w = 0;
    for (char c : s) w += (c == ' ' ? 0.3f : 0.6f);
    return {w * 16.0f * scale, 16.0f * scale, 12.8f * scale};
}

void VectorRenderer::richText(VectorCanvas& c, std::string_view text,
                              float x, float y, Color color, float scale,
                              float rotation, plot::HAlign lineAlign) {
    if (text.empty()) return;
    // Multi-line: emit each line, offset by the reference line height.
    const float lineH = measure("Mg", scale).height * 1.25f;
    size_t start = 0;
    int lineNo = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string_view line =
            text.substr(start, nl == std::string_view::npos
                                 ? std::string_view::npos : nl - start);
        float ly = y + lineNo * lineH;
        if (!volcano::text::containsMath(line)) {
            float lx = x;
            if (lineAlign != plot::HAlign::Left) {
                float w = measure(line, scale).width;
                lx -= lineAlign == plot::HAlign::Center ? w * 0.5f : w;
            }
            c.text({lx, ly}, line, 16.0f * scale, color, rotation);
        } else {
            auto lay = volcano::text::layoutMathText(line, scale,
                [this](std::string_view s, float sc) {
                    return measure(s, sc);
                });
            for (auto& run : lay.runs)
                c.text({x + run.x, ly + run.baseline}, run.text,
                       16.0f * run.scale, color, rotation);
            for (auto& r : lay.rules) {
                Point2D quad[4] = {
                    {x + r.x0, ly + r.y0}, {x + r.x1, ly + r.y0},
                    {x + r.x1, ly + r.y0 + r.thickness},
                    {x + r.x0, ly + r.y0 + r.thickness}};
                c.polygon(quad, color);
            }
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
        ++lineNo;
    }
}

void VectorRenderer::richTextFx(VectorCanvas& c,
                                std::span<const plot::PathEffect> fxs,
                                std::string_view text, float x, float y,
                                Color color, float scale, float rotation,
                                plot::HAlign lineAlign, float dpi) {
    if (text.empty()) return;
    auto normal = [&] {
        richText(c, text, x, y, color, scale, rotation, lineAlign);
    };
    if (fxs.empty()) { normal(); return; }
    for (const auto& fx : fxs) {
        auto off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case plot::PathEffect::Kind::Normal:
            normal();
            break;
        case plot::PathEffect::Kind::Stroke: {
            Color fc = fx.foreground.value_or(color);
            float rad = fx.strokeWidthPx(1.0f, dpi) * 0.5f;
            constexpr int kDirs = 8;
            for (int i = 0; i < kDirs; ++i) {
                float a = float(i) * 6.2831853f / float(kDirs);
                richText(c, text,
                         x + off.x + rad * std::cos(a),
                         y + off.y + rad * std::sin(a),
                         fc, scale, rotation, lineAlign);
            }
            break;
        }
        case plot::PathEffect::Kind::LineShadow:
        case plot::PathEffect::Kind::PatchShadow:
            richText(c, text, x + off.x, y + off.y,
                     fx.shadowFor(color), scale, rotation, lineAlign);
            break;
        }
        if (fx.thenNormal) normal();
    }
}

void VectorRenderer::fillRect(VectorCanvas& c, Rect2D r, Color col) {
    Point2D q[4] = {{float(r.x), float(r.y)},
                    {float(r.x + r.width), float(r.y)},
                    {float(r.x + r.width), float(r.y + r.height)},
                    {float(r.x), float(r.y + r.height)}};
    c.polygon(q, col);
}

void VectorRenderer::strokeRect(VectorCanvas& c, Rect2D r, const Pen& p) {
    Point2D q[5] = {{float(r.x), float(r.y)},
                    {float(r.x + r.width), float(r.y)},
                    {float(r.x + r.width), float(r.y + r.height)},
                    {float(r.x), float(r.y + r.height)},
                    {float(r.x), float(r.y)}};
    c.polyline(q, p);
}

void VectorRenderer::line(VectorCanvas& c, Point2D a, Point2D b,
                          const Pen& p) {
    Point2D pts[2] = {a, b};
    c.polyline(pts, p);
}

// ─── top-level ──────────────────────────────────────────────────────────────

void VectorRenderer::render(const plot::Figure& fig, VectorCanvas& canvas) {
    for (const auto* p : fig.axesDrawOrder()) {
        if (!p->axes->visible()) continue;
        emitAxes(*p->axes, p->axes->rect, canvas);
    }
    for (const auto& sf : fig.subfigs())
        render(*sf.figure, canvas);

    // Figure-level legend (mpl fig.legend) sits above all axes.
    emitFigureLegend(fig, canvas);

    // Figure-level texts (mpl fig.text / fig.texts).
    for (const auto& ft : fig.texts()) {
        if (!ft.visible || ft.detached || ft.text.empty()) continue;
        plot::Point2D pos;
        if (ft.transform) pos = ft.transform->apply({ft.x, ft.y});
        else if (ft.coords == plot::CoordSystem::Display)
            pos = {ft.x, ft.y};
        else
            pos = {ft.x * extent_.width,
                   (1.0f - ft.y) * extent_.height};
        pos.x += ft.dragOffset.x;
        pos.y += ft.dragOffset.y;
        const float fsc = ft.fontSize * fig.style().dpi / (72.0f*16.0f);
        auto m = measure(ft.text, fsc);
        if (ft.hasBbox || ft.bboxFaceColor.a > 0.0f) {
            auto al = plot::alignText(pos, ft.halign, ft.valign,
                                      m.width, m.height, m.ascent);
            emitTextBbox(canvas, al.x, al.y - m.ascent,
                         m.width, m.height, ft.bboxPadding,
                         ft.bboxFaceColor, ft.bboxEdgeColor,
                         ft.bboxEdgeWidth, ft.boxStyle,
                         ft.fontSize * fig.style().dpi / 72.0f);
        }
        auto dp = plot::alignText(pos, ft.halign, ft.valign,
                                  m.width, m.height, m.ascent);
        richTextFx(canvas, ft.pathEffects, ft.text, dp.x, dp.y, ft.color,
                   fsc, ft.rotation, ft.halign,
                   fig.style().dpi);
    }

    // Figure suptitle at top center.
    const auto& t = fig.style().title;
    if (!t.text.empty()) {
        float scale = t.font.size * fig.style().dpi / (72.0f * 16.0f);
        auto m = measure(t.text, scale);
        richText(canvas, t.text,
                 extent_.width * 0.5f - m.width * 0.5f, m.ascent + 2.0f,
                 t.color, scale, t.font.rotation, plot::HAlign::Center);
        if (t.weight == "bold" || t.font.weight == "bold")
            richText(canvas, t.text,
                     extent_.width * 0.5f - m.width * 0.5f + 0.6f,
                     m.ascent + 2.0f, t.color, scale, t.font.rotation,
                     plot::HAlign::Center);
    }

    // Figure supxlabel/supylabel — bottom center / left center rotated
    // (mirrors the raster Renderer placement).
    float labScale =
        fig.supXlabelFont.size * fig.style().dpi / (72.0f * 16.0f);
    if (!fig.supxlabel().empty()) {
        auto m = measure(fig.supxlabel(), labScale);
        richText(canvas, fig.supxlabel(),
                 extent_.width * 0.5f - m.width * 0.5f,
                 extent_.height - 4.0f - m.height + m.ascent,
                 fig.supXlabelColor, labScale, 0.0f,
                 plot::HAlign::Center);
    }
    labScale = fig.supYlabelFont.size * fig.style().dpi / (72.0f * 16.0f);
    if (!fig.supylabel().empty()) {
        auto m = measure(fig.supylabel(), labScale);
        richText(canvas, fig.supylabel(),
                 4.0f + m.ascent,
                 extent_.height * 0.5f + m.width * 0.5f,
                 fig.supYlabelColor, labScale,
                 -1.5707963267948966f /* -π/2, bottom-to-top */,
                 plot::HAlign::Center);
    }
}

void VectorRenderer::emitAxes(const plot::Axes& axes, Rect2D rect,
                              VectorCanvas& c) {
    const auto& style = axes.style();
    // Axes facecolor patch — mpl draws it iff axison && frameon.
    if (axes.axison() && axes.frameOn()) {
        auto fc = style.faceColor;
        if (auto a = axes.alpha()) fc.a *= *a;
        if (fc.a > 0.0f) fillRect(c, rect, fc);
    }

    // mpl axison=False drops the axis objects (grid, ticks, labels).
    if (axes.axison()) emitGrid(axes, rect, c);
    emitPlots(axes, rect, c);
    if (axes.axison()) emitSpinesTicks(axes, rect, c);
    emitLabels(axes, rect, c);
    emitAnnotations(axes, rect, c);
    emitLegend(axes, rect, c);
    emitSizeBars(axes, rect, c);
    emitInsetIndicators(axes, rect, c);
    emitAnchoredTexts(axes, rect, c);
    emitColorbar(axes, rect, c);
}

// ─── grid ───────────────────────────────────────────────────────────────────

void VectorRenderer::emitGrid(const plot::Axes& axes, Rect2D rect,
                              VectorCanvas& c) {
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    const auto& vp = axes.viewport();
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);

    auto emitAxisGrid = [&](const plot::AxisStyle& as,
                            const plot::AxisScale& scale,
                            float lo, float hi, bool vert) {
        if (!as.grid) return;
        const auto& tc = as.ticks;
        float axisLen = vert ? float(rect.width) : float(rect.height);
        auto majors = axisTicks(tc, scale, lo, hi, axisLen,
                                as.tickFont.size, figDpi, !vert);
        bool wantMajor = as.gridWhich != "minor";
        bool wantMinor = as.gridWhich != "major";
        auto emitSet = [&](std::span<const float> ticks, Color col,
                           float w, std::string_view ls) {
            Pen p = penOf(col, w, gridDashes(ls));
            for (float t : ticks) {
                auto f = vert ? axes.dataToFraction({t, 0.0f})
                              : axes.dataToFraction({0.0f, t});
                if (f.x < 0 || f.x > 1 || f.y < 0 || f.y > 1) continue;
                if (vert) {
                    float px = x0 + f.x * float(rect.width);
                    line(c, {px, y0}, {px, y1}, p);
                } else {
                    float py = y1 - f.y * float(rect.height);
                    line(c, {x0, py}, {x1, py}, p);
                }
            }
        };
        if (wantMajor)
            emitSet(majors, as.gridColor, as.gridLineWidth, as.gridLineStyle);
        if (wantMinor) {
            auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
            emitSet(minors, as.minorGridColor, as.minorGridLineWidth,
                    as.minorGridLineStyle);
        }
    };
    emitAxisGrid(style.xAxis, axes.xscale(), vp.x.min, vp.x.max, true);
    emitAxisGrid(style.yAxis, axes.yscale(), vp.y.min, vp.y.max, false);
}

// ─── plots (native vector + rasterized runs) ────────────────────────────────

void VectorRenderer::emitPlots(const plot::Axes& axes, Rect2D rect,
                               VectorCanvas& c) {
    auto order = axes.drawOrder();
    c.pushClip(rect);
    size_t i = 0;
    while (i < order.size()) {
        const auto* p = order[i];
        if (!p->visible) { ++i; continue; }  // mpl set_visible(False)
        if (p->canEmitVector() && !p->rasterized) {
            // mpl clip_on=False → emit outside the axes clip group.
            if (!p->clipOn) c.popClip();
            const_cast<plot::IPlot*>(p)->emitVector(c, axes, rect);
            if (!p->clipOn) c.pushClip(rect);
            ++i;
            continue;
        }
        // Contiguous run of rasterized/non-native layers → one image.
        std::vector<const plot::IPlot*> run;
        while (i < order.size() &&
               !(order[i]->canEmitVector() && !order[i]->rasterized)) {
            if (order[i]->visible) run.push_back(order[i]);
            ++i;
        }
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        if (rasterize_ && rasterize_(axes, run, rgba, w, h) && !rgba.empty())
            c.image({0, 0, int32_t(w), int32_t(h)}, w, h, rgba);
    }
    c.popClip();
}

// ─── spines + tick marks ────────────────────────────────────────────────────

void VectorRenderer::emitSpinesTicks(const plot::Axes& axes, Rect2D rect,
                                     VectorCanvas& c) {
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    if (!axes.axison()) return;
    if (!style.xAxis.visible && !style.yAxis.visible) return;

    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    Pen sp = penOf(style.xAxis.color, std::max(style.xAxis.lineWidth, 1.0f));
    // mpl frame_on=False removes the spine frame; tick marks remain.
    const auto& spv = axes.spines();
    if (axes.frameOn()) {
        // mpl Spine.set_position / set_bounds / set_color / set_linewidth.
        auto emitSpine = [&](std::string_view side, bool horiz,
                             float defPos) {
            const auto& s = spv.side(side);
            if (!s.visible) return;
            auto g = axes.spineLine(side, rect);
            Pen pen = sp;
            if (s.color) pen.color = *s.color;
            if (s.lineWidth) pen.width = std::max(*s.lineWidth, 1.0f);
            // mpl set_linestyle/set_dashes on a spine.
            if (!s.dashes.empty()) pen.dashes = s.dashes;
            else if (s.lineStyle && *s.lineStyle != plot::LineStyle::Solid)
                pen.dashes = plot::dashPattern(*s.lineStyle, pen.width);
            float p = s.positionSet ? g.pos : defPos;
            float from = s.positionSet || s.bounds ? g.from
                       : (horiz ? x0 : y0);
            float to = s.positionSet || s.bounds ? g.to
                     : (horiz ? x1 : y1);
            if (horiz) line(c, {from, p}, {to, p}, pen);
            else       line(c, {p, from}, {p, to}, pen);
        };
        emitSpine("bottom", true,  y1);
        emitSpine("top",    true,  y0);
        emitSpine("left",   false, x0);
        emitSpine("right",  false, x1);
    }

    const auto& vp = axes.viewport();
    auto tickMarks = [&](const plot::AxisStyle& as,
                         const plot::AxisScale& scale,
                         float lo, float hi, bool yAxis, bool farSide) {
        const auto& tc = as.ticks;
        float inF = tickInFrac(tc);
        auto emitSet = [&](std::span<const float> ticks, float sizePt,
                           float widthPt) {
            Pen p = penOf(as.color, std::max(widthPt * figDpi / 72.0f, 1.0f));
            float len = sizePt * figDpi / 72.0f;
            // mpl Spine.set_position: tick marks ride on the spine.
            float edgeOverride = std::numeric_limits<float>::quiet_NaN();
            const auto& spec =
                spv.side(yAxis ? (farSide ? "right" : "left")
                               : (farSide ? "top" : "bottom"));
            if (spec.positionSet)
                edgeOverride = axes.spineLine(
                    yAxis ? (farSide ? "right" : "left")
                          : (farSide ? "top" : "bottom"), rect).pos;
            for (float t : ticks) {
                auto f = yAxis ? axes.dataToFraction({0.0f, t})
                               : axes.dataToFraction({t, 0.0f});
                if (yAxis) {
                    float py = y1 - f.y * float(rect.height);
                    if (py < y0 || py > y1) continue;
                    float edge = std::isfinite(edgeOverride) ? edgeOverride
                                 : (farSide ? x1 : x0);
                    float dir = farSide ? 1.0f : -1.0f;
                    // `inF` of the length goes inside, rest outside.
                    line(c, {edge + dir * len * (1.0f - inF), py},
                         {edge - dir * len * inF, py}, p);
                } else {
                    float px = x0 + f.x * float(rect.width);
                    if (px < x0 || px > x1) continue;
                    float edge = std::isfinite(edgeOverride) ? edgeOverride
                                 : (farSide ? y0 : y1);
                    float dir = farSide ? -1.0f : 1.0f;
                    line(c, {px, edge + dir * len * (1.0f - inF)},
                         {px, edge - dir * len * inF}, p);
                }
            }
        };
        float axisLen = yAxis ? float(rect.height) : float(rect.width);
        auto majors = axisTicks(tc, scale, lo, hi, axisLen,
                                as.tickFont.size, figDpi, yAxis);
        emitSet(majors, tc.majorSize, tc.majorWidth);
        auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
        emitSet(minors, tc.minorSize, tc.minorWidth);
    };
    // mpl label_outer(remove_inner_ticks=True): drop inner tick marks.
    const auto& xf = axes.xFurniture();
    const auto& yf = axes.yFurniture();
    if (style.xAxis.visible &&
        !(axes.innerTicksRemoved() && axes.xTickLabelsHidden())) {
        if (xf.marksFar)
            tickMarks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                      false, true);
        if (xf.marksNear)
            tickMarks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                      false, false);
    }
    if (style.yAxis.visible &&
        !(axes.innerTicksRemoved() && axes.yTickLabelsHidden())) {
        if (yf.marksFar)
            tickMarks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                      true, true);
        if (yf.marksNear)
            tickMarks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                      true, false);
    }
}

// ─── labels, tick labels, title ─────────────────────────────────────────────

void VectorRenderer::emitLabels(const plot::Axes& axes, Rect2D rect,
                                VectorCanvas& c) {
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    const auto& vp = axes.viewport();
    // mpl per-element sizes (pt) → canvas scale (scale 1.0 = 16px):
    // xtick/ytick.labelsize, axes.labelsize, axes.titlesize.
    const float ptToScale = figDpi / (72.0f * 16.0f);
    const float xTickScale = style.xAxis.tickFont.size * ptToScale;
    const float yTickScale = style.yAxis.tickFont.size * ptToScale;
    const float xLabelScale = style.xAxis.labelFont.size * ptToScale;
    const float yLabelScale = style.yAxis.labelFont.size * ptToScale;
    const float titleScale = style.title.font.size * ptToScale;
    const Color labelColor = style.textColor;
    // mpl tick_params labelcolor/colors override the default tick-label
    // color (and the offset text color) per axis.
    const Color xTickColor =
        style.xAxis.ticks.labelColor.value_or(labelColor);
    const Color yTickColor =
        style.yAxis.ticks.labelColor.value_or(labelColor);
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    constexpr float kTickLength = 4.0f;

    // mpl axison=False: only the title is drawn (it is not part of the
    // axis objects).
    const bool axisOff = !axes.axison();
    // mpl label_outer: inner rows/columns suppress tick labels.
    const bool hideXTickLabels = axes.xTickLabelsHidden();
    const bool hideYTickLabels = axes.yTickLabelsHidden();

    // mpl: axis labels ride on the repositioned spine (set_position)
    // and set_label_position picks the near/far edge.
    const auto& xf = axes.xFurniture();
    const auto& yf = axes.yFurniture();
    float xLabelEdge, yLabelEdge;
    {
        const char* xSide = xf.labelFar ? "top" : "bottom";
        const char* ySide = yf.labelFar ? "right" : "left";
        xLabelEdge = xf.labelFar ? y0 : y1;
        yLabelEdge = yf.labelFar ? x1 : x0;
        const auto& xs = axes.spines().side(xSide);
        const auto& ys = axes.spines().side(ySide);
        if (xs.positionSet) xLabelEdge = axes.spineLine(xSide, rect).pos;
        if (ys.positionSet) yLabelEdge = axes.spineLine(ySide, rect).pos;
    }

    if (!axisOff && style.xAxis.visible && !style.xAxis.label.empty()) {
        auto m = measure(style.xAxis.label, xLabelScale);
        float d = xf.labelFar ? -1.0f : 1.0f;
        float cy = xLabelEdge + d * (kTickLength +
                     style.xAxis.ticks.majorPad * figDpi / 72.0f + 16.0f +
                     style.xAxis.labelPad * figDpi / 72.0f) +
                   (xf.labelFar ? -m.height + m.ascent : m.ascent);
        richText(c, style.xAxis.label,
                 rect.x + rect.width / 2.0f - m.width / 2.0f, cy,
                 labelColor, xLabelScale, style.xAxis.labelFont.rotation,
                 plot::HAlign::Center);
    }
    if (!axisOff && style.yAxis.visible && !style.yAxis.label.empty()) {
        auto m = measure(style.yAxis.label, yLabelScale);
        float oy = y0 + (y1 - y0) / 2.0f + m.width / 2.0f;
        float d = yf.labelFar ? 1.0f : -1.0f;
        float centerPos = yLabelEdge + d * (kTickLength +
                          style.yAxis.ticks.majorPad * figDpi / 72.0f + 40.0f +
                          style.yAxis.labelPad * figDpi / 72.0f);
        float ox = centerPos - m.height / 2.0f + m.ascent;
        constexpr float kRotMinus90 = -1.5707963267948966f;
        richText(c, style.yAxis.label, ox, oy, labelColor, yLabelScale,
                 kRotMinus90 + style.yAxis.labelFont.rotation,
                 plot::HAlign::Center);
    }
    if (!style.title.text.empty()) {
        auto m = measure(style.title.text, titleScale);
        float tx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        float ty = y0 - style.title.pad * figDpi / 72.0f - m.height + m.ascent;
        richText(c, style.title.text, tx, ty,
                 style.title.color, titleScale, style.title.font.rotation,
                 plot::HAlign::Center);
        // Faux bold: second pass offset ~0.6px (mirrors raster).
        if (style.title.weight == "bold" ||
            style.title.font.weight == "bold")
            richText(c, style.title.text, tx + 0.6f, ty,
                     style.title.color, titleScale, style.title.font.rotation,
                     plot::HAlign::Center);
    }

    // X tick labels — mpl draws them per enabled side (labelsNear /
    // labelsFar); each side's labels ride on that side's spine.
    if (!axisOff && !hideXTickLabels && style.xAxis.visible) {
        const auto& tc = style.xAxis.ticks;
        auto xTicks = axisTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                float(rect.width), style.xAxis.tickFont.size,
                                figDpi, false);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        auto* fmt = axisFormatter(tc, xTicks, style, axes.xscale(),
                                  vp.x.min, vp.x.max,
                                  defaultFmt, strFmt, logFmt);
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * figDpi / 72.0f;
        auto emitSide = [&](bool top) {
            float edge = top ? y0 : y1;
            {
                const auto& spec =
                    axes.spines().side(top ? "top" : "bottom");
                if (spec.positionSet)
                    edge = axes.spineLine(top ? "top" : "bottom",
                                          rect).pos;
            }
            float d = top ? -1.0f : 1.0f;
            float tickEnd = edge + d * outLen;
            int i = 0;
            for (float tick : xTicks) {
                float px = x0 + axes.dataToFraction({tick, 0.0f}).x *
                                rect.width;
                if (px < x0 || px > x1) { ++i; continue; }
                auto label = tickLabel(tc, *fmt, tick, i++);
                if (label.empty() || tc.hiddenLabels.contains(i - 1))
                    continue;
                auto m = measure(label, xTickScale);
                float rot = style.xAxis.tickFont.rotation;
                float baseY = top ? tickEnd - kTickSpacing - m.height +
                                        m.ascent
                                  : tickEnd + kTickSpacing + m.ascent;
                float x, y = baseY;
                if (rot != 0.0f) {
                    // Rotated: anchor the baseline's right end at the
                    // tick (mpl ha='right' for rotated xticklabels).
                    float cosR = std::cos(rot), sinR = std::sin(rot);
                    x = px - m.width * cosR;
                    y = baseY - m.width * sinR;
                } else {
                    x = px - m.width * 0.5f;
                }
                richText(c, label, x, y, xTickColor, xTickScale, rot);
            }
            auto off = fixMinus(fmt->offsetText());
            if (!off.empty()) {
                auto m = measure(off, xTickScale * 0.8f);
                float x = x1 - m.width * 0.5f;
                float y = top ? y0 - outLen - kTickSpacing - m.height +
                                    m.ascent
                              : tickEnd + kTickSpacing + m.height +
                                    m.ascent * 0.2f;
                richText(c, off, x, y, xTickColor, xTickScale * 0.8f);
            }
            if (logMinorLabels(tc, axes.xscale())) {
                auto minor = axisMinorTicks(tc, axes.xscale(),
                                            vp.x.min, vp.x.max, xTicks);
                plot::LogFormatterSciNotation defMinorFmt;
                plot::Formatter* mfmt = tc.minorFormatter
                                            ? tc.minorFormatter.get()
                                            : &defMinorFmt;
                mfmt->setViewInterval(vp.x.min, vp.x.max);
                mfmt->setLocs(minor);
                float mEnd = edge + d * tc.minorSize * figDpi / 72.0f *
                                (1.0f - tickInFrac(tc));
                float mGap = tc.minorPad * figDpi / 72.0f;
                int mi = 0;
                for (float t : minor) {
                    float px = x0 + axes.dataToFraction({t, 0.0f}).x *
                                    rect.width;
                    if (px < x0 || px > x1) { ++mi; continue; }
                    auto label = fmtLabel(*mfmt, t, mi++);
                    if (label.empty() ||
                        tc.hiddenMinorLabels.contains(mi - 1))
                        continue;
                    auto m = measure(label, xTickScale);
                    richText(c, label, px - m.width * 0.5f,
                             mEnd + mGap + m.ascent, xTickColor, xTickScale);
                }
            }
        };
        if (xf.labelsNear) emitSide(false);
        if (xf.labelsFar) emitSide(true);
    }

    // Y tick labels.
    if (!axisOff && !hideYTickLabels && style.yAxis.visible) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                float(rect.height), style.yAxis.tickFont.size,
                                figDpi, true);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        auto* fmt = axisFormatter(tc, yTicks, style, axes.yscale(),
                                  vp.y.min, vp.y.max,
                                  defaultFmt, strFmt, logFmt);
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * figDpi / 72.0f;
        // mpl: tick labels per enabled side; each rides on its spine.
        auto emitSide = [&](bool right) {
            float edge = right ? x1 : x0;
            {
                const auto& spec =
                    axes.spines().side(right ? "right" : "left");
                if (spec.positionSet)
                    edge = axes.spineLine(right ? "right" : "left",
                                          rect).pos;
            }
            float d = right ? 1.0f : -1.0f;
            float tickEnd = edge + d * outLen;
            int i = 0;
            for (float tick : yTicks) {
                float py = y1 - axes.dataToFraction({0.0f, tick}).y *
                                rect.height;
                if (py < y0 || py > y1) { ++i; continue; }
                auto label = tickLabel(tc, *fmt, tick, i++);
                if (label.empty() || tc.hiddenLabels.contains(i - 1))
                    continue;
                auto m = measure(label, yTickScale);
                float rot = style.yAxis.tickFont.rotation;
                float baseY = py + m.ascent - m.height * 0.5f;
                float x, y = baseY;
                if (rot != 0.0f) {
                    float cosR = std::cos(rot), sinR = std::sin(rot);
                    float e2 = right ? tickEnd + kTickSpacing
                                     : tickEnd - kTickSpacing;
                    x = right ? e2 : e2 - m.width * cosR;
                    y = right ? baseY : baseY - m.width * sinR;
                } else {
                    x = right ? tickEnd + kTickSpacing
                              : tickEnd - kTickSpacing - m.width;
                }
                richText(c, label, x, y, yTickColor, yTickScale, rot);
            }
            auto off = fixMinus(fmt->offsetText());
            if (!off.empty()) {
                auto m = measure(off, yTickScale * 0.8f);
                float x = right ? x1 - m.width : x0;
                richText(c, off, x, y0 - kTickSpacing, yTickColor,
                         yTickScale * 0.8f);
            }
            if (logMinorLabels(tc, axes.yscale())) {
                auto minor = axisMinorTicks(tc, axes.yscale(),
                                            vp.y.min, vp.y.max, yTicks);
                plot::LogFormatterSciNotation defMinorFmt;
                plot::Formatter* mfmt = tc.minorFormatter
                                            ? tc.minorFormatter.get()
                                            : &defMinorFmt;
                mfmt->setViewInterval(vp.y.min, vp.y.max);
                mfmt->setLocs(minor);
                float mEnd = edge + d * tc.minorSize * figDpi / 72.0f *
                                (1.0f - tickInFrac(tc));
                float mGap = tc.minorPad * figDpi / 72.0f;
                int mi = 0;
                for (float t : minor) {
                    float py = y1 - axes.dataToFraction({0.0f, t}).y *
                                    rect.height;
                    if (py < y0 || py > y1) { ++mi; continue; }
                    auto label = fmtLabel(*mfmt, t, mi++);
                    if (label.empty() ||
                        tc.hiddenMinorLabels.contains(mi - 1))
                        continue;
                    auto m = measure(label, yTickScale);
                    richText(c, label,
                             right ? mEnd + mGap : mEnd - mGap - m.width,
                             py + m.ascent - m.height * 0.5f, yTickColor,
                             yTickScale);
                }
            }
        };
        if (yf.labelsNear) emitSide(false);
        if (yf.labelsFar) emitSide(true);

        // --- Secondary y axis (mpl secondary_yaxis): right-side tick
        // labels in transformed units via the inverse map.
        if (auto sec = axes.secondaryY()) {
            float slo = sec->forward(vp.y.min), shi = sec->forward(vp.y.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.height), style.yAxis.tickFont.size,
                                    figDpi, true);
            plot::ScalarFormatter sFmt;
            sFmt.setLocs(sTicks);
            float rEdge = x1;
            for (float st : sTicks) {
                float vv = sec->inverse(st);
                float py = y1 -
                           axes.dataToFraction({0.0f, vv}).y * rect.height;
                if (py < y0 || py > y1) continue;
                auto label = sFmt.format(st, 0);
                if (label.empty()) continue;
                auto m = measure(label, yTickScale);
                richText(c, label, rEdge + outLen + kTickSpacing,
                         py + m.ascent - m.height * 0.5f, yTickColor, yTickScale);
            }
        }
    }

    // --- Secondary x axis (mpl secondary_xaxis): top-side tick labels.
    if (auto sec = axes.secondaryX(); sec && !axisOff) {
        const auto& tc = style.xAxis.ticks;
        float slo = sec->forward(vp.x.min), shi = sec->forward(vp.x.max);
        auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                float(rect.width), style.xAxis.tickFont.size,
                                figDpi, false);
        plot::ScalarFormatter sFmt;
        sFmt.setLocs(sTicks);
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * figDpi / 72.0f;
        for (float st : sTicks) {
            float vv = sec->inverse(st);
            float px = x0 +
                       axes.dataToFraction({vv, 0.0f}).x * rect.width;
            if (px < x0 || px > x1) continue;
            auto label = sFmt.format(st, 0);
            if (label.empty()) continue;
            auto m = measure(label, xTickScale);
            richText(c, label, px - m.width * 0.5f,
                     y0 - outLen - kTickSpacing - m.height + m.ascent,
                     yTickColor, xTickScale);
        }
    }
}

// ─── annotations ────────────────────────────────────────────────────────────

void VectorRenderer::emitAnnotations(const plot::Axes& axes, Rect2D rect,
                                     VectorCanvas& c) {
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    plot::Extent2D figExtent = extent_;
    float dpi = figDpi;
    auto clipped = [&](const auto& fn, bool clipOn) {
        if (clipOn) c.pushClip(rect);
        fn();
        if (clipOn) c.popClip();
    };

    for (const auto& t : axes.texts()) {
        if (!t.visible || t.detached || t.text.empty()) continue;
        // mpl transform= overrides the coord system entirely.
        auto pos = t.transform
            ? t.transform->apply({t.x, t.y})
            : plot::toDisplay(t.x, t.y, t.coords, rect, figExtent, axes,
                              dpi, t.xyOffsetX, t.xyOffsetY);
        auto m = measure(t.text, t.fontSize * dpi / (72.0f * 16.0f));
        {
            auto al = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            t.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                         uint32_t(m.width), uint32_t(m.height)};
        }
        clipped([&] {
            if (t.hasBbox || t.bboxFaceColor.a > 0.0f) {
                auto al = plot::alignText(pos, t.halign, t.valign,
                                          m.width, m.height, m.ascent);
                emitTextBbox(c, al.x, al.y - m.ascent,
                             m.width, m.height, t.bboxPadding,
                             t.bboxFaceColor, t.bboxEdgeColor,
                             t.bboxEdgeWidth, t.boxStyle,
                             t.fontSize * dpi / 72.0f);
            }
            auto dp = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            richTextFx(c, t.pathEffects, t.text, dp.x, dp.y, t.color,
                       t.fontSize * dpi / (72.0f * 16.0f), t.rotation,
                       t.halign, dpi);
        }, t.clipOn);
    }

    for (const auto& a : axes.annotations()) {
        if (a.detached) continue;
        auto dataPos = plot::toDisplay(a.xy[0], a.xy[1], a.xyCoords,
                                       rect, figExtent, axes, dpi);
        auto textPos = plot::toDisplay(a.xyText[0], a.xyText[1],
                                       a.xyTextCoords, rect, figExtent, axes,
                                       dpi, a.textOffsetX, a.textOffsetY);
        textPos.x += a.dragOffset.x;
        textPos.y += a.dragOffset.y;
        clipped([&] {
            if (a.arrowStyle != plot::ArrowStyle::None) {
                auto path = plot::connectionPath(textPos, dataPos,
                                                 a.connection,
                                                 a.shrinkA, a.shrinkB);
                if (path.size() >= 2 && a.arrowSpec) {
                    auto spec = *a.arrowSpec;
                    spec.mutationSize *= dpi / 72.0f;
                    auto geo = plot::buildArrowGeometry(
                        path, spec, a.arrowWidth);
                    // Optional artist clip path (data coords → px ring).
                    std::vector<plot::Point2D> ring;
                    if (a.clipPath) {
                        auto subs = a.clipPath->toPolylines();
                        const plot::Path::Subpath* best = nullptr;
                        for (auto& sp : subs)
                            if (!best ||
                                sp.points.size() > best->points.size())
                                best = &sp;
                        if (best && best->points.size() >= 3)
                            for (auto p : best->points) {
                                auto f2 = axes.dataToFraction(p);
                                ring.push_back(
                                    {rect.x + f2.x * float(rect.width),
                                     rect.y + (1.0f - f2.y) *
                                                  float(rect.height)});
                            }
                    }
                    Pen p = penOf(a.arrowColor, a.arrowWidth);
                    for (auto& s : geo.strokes) {
                        if (ring.empty()) {
                            c.polyline(s, p);
                        } else {
                            for (auto& piece :
                                 plot::clipPolylineToPolygon(
                                     std::span<const plot::Point2D>{s},
                                     ring))
                                c.polyline(piece, p);
                        }
                    }
                    for (auto& f : geo.fills) {
                        if (ring.empty()) {
                            c.polygon(f, a.arrowColor);
                        } else {
                            auto tris = plot::clipTrianglesToPolygon(
                                plot::earClip(f), ring);
                            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                                plot::Point2D t[3] = {tris[i], tris[i+1],
                                                      tris[i+2]};
                                c.polygon(t, a.arrowColor);
                            }
                        }
                    }
                } else if (path.size() >= 2) {
                    Pen p = penOf(a.arrowColor, a.arrowWidth);
                    c.polyline(path, p);
                    size_t n = path.size();
                    float ux = path[n-1].x - path[n-2].x;
                    float uy = path[n-1].y - path[n-2].y;
                    float tl = std::hypot(ux, uy);
                    if (tl > 1e-4f) {
                        ux /= tl; uy /= tl;
                        float ex = path[n-1].x, ey = path[n-1].y;
                        float hl = a.arrowHeadSize;
                        float ha = a.arrowHeadAngle * float(M_PI) / 180.0f;
                        float ca = std::cos(ha), sa = std::sin(ha);
                        line(c, {ex, ey},
                             {ex - (ux*ca - uy*sa) * hl,
                              ey - (ux*sa + uy*ca) * hl}, p);
                        line(c, {ex, ey},
                             {ex - (ux*ca + uy*sa) * hl,
                              ey - (-ux*sa + uy*ca) * hl}, p);
                    }
                }
            }
            if (!a.text.empty()) {
                auto m = measure(a.text,
                                 a.fontSize * dpi / (72.0f * 16.0f));
                {
                    auto al = plot::alignText(textPos, a.halign, a.valign,
                                              m.width, m.height, m.ascent);
                    a.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                                 uint32_t(m.width), uint32_t(m.height)};
                }
                if (a.hasBbox || a.bboxFaceColor.a > 0.0f) {
                    auto al = plot::alignText(textPos, a.halign, a.valign,
                                              m.width, m.height, m.ascent);
                    emitTextBbox(c, al.x, al.y - m.ascent,
                                 m.width, m.height, a.bboxPadding,
                                 a.bboxFaceColor, a.bboxEdgeColor,
                                 a.bboxEdgeWidth, a.boxStyle,
                                 a.fontSize * dpi / 72.0f);
                }
                auto dp = plot::alignText(textPos, a.halign, a.valign,
                                          m.width, m.height, m.ascent);
                richTextFx(c, a.pathEffects, a.text, dp.x, dp.y, a.color,
                           a.fontSize * dpi / (72.0f * 16.0f), 0.0f,
                           a.halign, dpi);
            }
        }, a.clipOn);
    }
}

// ─── legend ─────────────────────────────────────────────────────────────────


void VectorRenderer::emitSizeBars(const plot::Axes& axes, Rect2D rect,
                                  VectorCanvas& c) {
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    if (axes.sizeBars().empty()) return;
    const float dpi = figDpi;
    auto measureFn = [this, dpi](std::string_view t, float pt) {
        auto m = measure(t, pt * dpi / (72.0f * 16.0f));
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& sb : axes.sizeBars()) {
        if (sb.detached) continue;
        auto L = plot::layoutSizeBar(sb, axes, rect, extent_, dpi,
                                     measureFn);
        if (!L.valid) continue;
        auto toI = [](plot::Rect2Df r) {
            return Rect2D{int32_t(std::lround(r.x)),
                          int32_t(std::lround(r.y)),
                          uint32_t(std::lround(r.w)),
                          uint32_t(std::lround(r.h))};
        };
        if (sb.frameon) {
            fillRect(c, toI(L.box), sb.frameFaceColor);
            strokeRect(c, toI(L.box), penOf(sb.frameEdgeColor, 1.0f));
        }
        if (L.fill)
            fillRect(c, toI(L.bar), sb.color);
        else
            strokeRect(c, toI(L.bar), penOf(sb.color, 1.0f));
        if (!sb.label.empty())
            richText(c, sb.label, L.labelBaseline.x, L.labelBaseline.y,
                     sb.color, sb.fontSize * dpi / (72.0f * 16.0f));
    }
}


void VectorRenderer::emitInsetIndicators(const plot::Axes& axes,
                                         Rect2D rect, VectorCanvas& c) {
    if (axes.insetIndicators().empty()) return;
    for (const auto& ind : axes.insetIndicators()) {
        auto L = plot::layoutInsetIndicator(ind, axes, rect, extent_);
        if (!L.valid) continue;
        auto edge = ind.edgeColor;
        edge.a *= ind.alpha;
        auto face = ind.faceColor;
        face.a *= ind.alpha;
        Point2D q[5] = {{L.rect.x, L.rect.y},
                        {L.rect.x + L.rect.w, L.rect.y},
                        {L.rect.x + L.rect.w, L.rect.y + L.rect.h},
                        {L.rect.x, L.rect.y + L.rect.h},
                        {L.rect.x, L.rect.y}};
        if (face.a > 0.0f)
            c.polygon(std::span{q, 4}, face);
        c.polyline(std::span{q, 5}, penOf(edge, ind.lineWidth));
        for (int i = 0; i < 4; ++i) {
            if (!L.connVisible[i]) continue;
            Point2D seg[2] = {L.connectors[i].first,
                              L.connectors[i].second};
            c.polyline(std::span{seg}, penOf(edge, ind.lineWidth));
        }
    }
}


void VectorRenderer::emitAnchoredTexts(const plot::Axes& axes, Rect2D rect,
                                       VectorCanvas& c) {
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    if (axes.anchoredTexts().empty()) return;
    const float dpi = figDpi;
    auto measureFn = [this, dpi](std::string_view t, float pt) {
        auto m = measure(t, pt * dpi / (72.0f * 16.0f));
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& at : axes.anchoredTexts()) {
        if (at.detached) continue;
        auto L = plot::layoutAnchoredText(at, axes, rect, dpi, measureFn);
        if (!L.valid) continue;
        if (at.frameon) {
            Rect2D box{int32_t(std::lround(L.box.x)),
                       int32_t(std::lround(L.box.y)),
                       uint32_t(std::lround(L.box.w)),
                       uint32_t(std::lround(L.box.h))};
            fillRect(c, box, at.frameFaceColor);
            strokeRect(c, box, penOf(at.frameEdgeColor, 1.0f));
        }
        size_t i = 0, start = 0;
        while (true) {
            size_t nl = at.text.find('\n', start);
            auto line = std::string_view(at.text).substr(
                start, nl == std::string_view::npos
                           ? std::string_view::npos : nl - start);
            richText(c, line, L.lineBaselines[i].x,
                     L.lineBaselines[i].y, at.color,
                     at.fontSize * dpi / (72.0f * 16.0f));
            ++i;
            if (nl == std::string_view::npos) break;
            start = nl + 1;
        }
    }
}

/// One legend row for vector output (mirrors raster LegendEntry).
struct VectorRenderer::LegendVecEntry {
    std::string label;
    plot::Color color;
    plot::LegendMarker marker;
    int points = -1;
};

std::vector<VectorRenderer::LegendVecEntry>
VectorRenderer::collectVecLegendEntries(const plot::Axes& axes) {
    const auto& lg = axes.style().legend;
    std::vector<LegendVecEntry> entries;
    if (lg.explicitHandles) {
        for (auto& h : *lg.explicitHandles)
            entries.push_back({h.label, h.color, h.marker, h.points});
    } else {
        for (auto& plot : axes.plots()) {
            if (auto it = lg.handlerMap.find(std::type_index(typeid(*plot)));
                it != lg.handlerMap.end()) {
                for (auto& h : it->second(*plot))
                    entries.push_back(
                        {std::move(h.label), h.color, h.marker, h.points});
                continue;
            }
            for (auto& h : plot->legendEntries())
                entries.push_back(
                    {std::move(h.label), h.color, h.marker, h.points});
        }
    }
    for (size_t i = 0; i < lg.explicitLabels.size() && i < entries.size();
         ++i)
        entries[i].label = lg.explicitLabels[i];
    if (lg.reverse)
        std::ranges::reverse(entries);
    return entries;
}

void VectorRenderer::emitLegend(const plot::Axes& axes, Rect2D rect,
                                VectorCanvas& c) {
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    const auto& lg = style.legend;
    if (!lg.visible) return;

    auto entries = collectVecLegendEntries(axes);
    if (entries.empty()) return;

    // loc + anchor resolution needs fontPx for borderaxespad.
    const float fontPx = lg.font.size * figDpi / 72.0f;

    struct LocAnchor { float fx, fy, bx, by; };
    auto parseLoc = [](std::string_view loc) -> LocAnchor {
        if (loc == "upper left" || loc == "2")   return {0, 1, 0, 1};
        if (loc == "lower left" || loc == "3")   return {0, 0, 0, 0};
        if (loc == "lower right" || loc == "4")  return {1, 0, 1, 0};
        if (loc == "center left" || loc == "6")  return {0, 0.5f, 0, 0.5f};
        if (loc == "right" || loc == "center right" || loc == "5" ||
            loc == "7")                        return {1, 0.5f, 1, 0.5f};
        if (loc == "lower center" || loc == "8") return {0.5f, 0, 0.5f, 0};
        if (loc == "upper center" || loc == "9") return {0.5f, 1, 0.5f, 1};
        if (loc == "center" || loc == "10")      return {0.5f, 0.5f, 0.5f, 0.5f};
        return {1, 1, 1, 1};
    };
    const auto la = parseLoc(lg.location);
    const bool hasAnchor = lg.anchorX >= 0.0f || lg.anchorY >= 0.0f;
    float afx = hasAnchor ? lg.anchorX : la.fx;
    float afy = hasAnchor ? lg.anchorY : la.fy;
    const bool sub = hasAnchor && lg.anchorW >= 0.0f && lg.anchorH >= 0.0f;
    float px, py, subW = -1.0f;
    if (hasAnchor && lg.anchorSpace == plot::CoordSystem::Figure) {
        if (sub) {
            px = (afx + la.fx * lg.anchorW) * extent_.width;
            py = (1.0f - afy - la.fy * lg.anchorH) * extent_.height;
            subW = lg.anchorW * extent_.width;
        } else {
            px = afx * extent_.width;
            py = (1.0f - afy) * extent_.height;
        }
    } else {
        if (sub) {
            px = rect.x + (afx + la.fx * lg.anchorW) * rect.width;
            py = rect.y + (1.0f - afy - la.fy * lg.anchorH) * rect.height;
            subW = lg.anchorW * rect.width;
        } else {
            px = rect.x + afx * rect.width;
            py = rect.y + (1.0f - afy) * rect.height;
        }
    }
    if (!hasAnchor) {
        const float m = lg.borderAxesPad * fontPx;
        if (la.bx > 0.5f) px -= m; else if (la.bx < 0.5f) px += m;
        if (la.by > 0.5f) py += m; else if (la.by < 0.5f) py -= m;
    }
    float forceW = -1.0f;
    if (lg.expand)
        forceW = subW >= 0.0f ? subW
            : float(rect.width) - 2.0f * lg.borderAxesPad * fontPx;
    emitLegendBox(c, entries, lg, style.textColor, {px, py},
                  la.bx, la.by, forceW, figDpi);
}

/// mpl fig.legend — figure-level legend anchored in figure space.
void VectorRenderer::emitFigureLegend(const plot::Figure& fig,
                                      VectorCanvas& c) {
    const auto& lg = fig.figureLegend();
    if (!lg.visible) return;

    std::vector<LegendVecEntry> entries;
    for (const auto* p : fig.axesDrawOrder()) {
        if (!p->axes->visible()) continue;
        auto es = collectVecLegendEntries(*p->axes);
        entries.insert(entries.end(),
                       std::make_move_iterator(es.begin()),
                       std::make_move_iterator(es.end()));
    }
    if (entries.empty()) return;

    struct LocAnchor { float fx, fy, bx, by; };
    auto parseLoc = [](std::string_view loc) -> LocAnchor {
        if (loc == "upper left" || loc == "2")   return {0, 1, 0, 1};
        if (loc == "lower left" || loc == "3")   return {0, 0, 0, 0};
        if (loc == "lower right" || loc == "4")  return {1, 0, 1, 0};
        if (loc == "center left" || loc == "6")  return {0, 0.5f, 0, 0.5f};
        if (loc == "right" || loc == "center right" || loc == "5" ||
            loc == "7")                        return {1, 0.5f, 1, 0.5f};
        if (loc == "lower center" || loc == "8") return {0.5f, 0, 0.5f, 0};
        if (loc == "upper center" || loc == "9") return {0.5f, 1, 0.5f, 1};
        if (loc == "center" || loc == "10")      return {0.5f, 0.5f, 0.5f, 0.5f};
        return {1, 1, 1, 1};
    };
    const auto la = parseLoc(lg.location);
    const bool hasAnchor = lg.anchorX >= 0.0f || lg.anchorY >= 0.0f;
    const float afx = hasAnchor ? lg.anchorX : la.fx;
    const float afy = hasAnchor ? lg.anchorY : la.fy;
    const bool sub = hasAnchor && lg.anchorW >= 0.0f && lg.anchorH >= 0.0f;
    Point2D anchor = sub
        ? Point2D{(afx + la.fx * lg.anchorW) * extent_.width,
                  (1.0f - afy - la.fy * lg.anchorH) * extent_.height}
        : Point2D{afx * extent_.width, (1.0f - afy) * extent_.height};
    const float forceW = lg.expand
        ? (sub ? lg.anchorW * extent_.width : extent_.width)
        : -1.0f;
    emitLegendBox(c, entries, lg, fig.style().textColor, anchor,
                  la.bx, la.by, forceW, fig.style().dpi);
}

/// Paint a legend box whose (bx,by) box-fraction corner sits at `anchor`
/// (canvas px) — shared by the axes and figure legends.
void VectorRenderer::emitLegendBox(VectorCanvas& c,
        const std::vector<LegendVecEntry>& entries,
        const plot::LegendStyle& lg, plot::Color textColor,
        Point2D anchor, float bx, float by, float forceW, float dpi) {
    const float scale = lg.font.size * dpi / (72.0f * 16.0f);
    const float fontPx = 16.0f * scale;
    const float pad = lg.borderPad * fontPx + (lg.fancyBox ? 2.0f : 0.0f);
    const float handleW = lg.handleLength * fontPx;
    const float textGap = lg.handleTextPad * fontPx;
    const float colGap = lg.columnSpacing * fontPx;
    const float rowSep = lg.labelSpacing * fontPx;

    // mpl handle box extents around the text baseline (see drawLegend).
    const float hBelow = 0.35f * fontPx * (lg.handleHeight - 0.7f);
    const float hBoxH = lg.handleHeight * fontPx - hBelow;
    const float hAbove = hBoxH - hBelow;

    const int n = int(entries.size());
    const int rows = lg.nrows > 0 ? lg.nrows
        : (n + std::max(1, lg.ncols) - 1) / std::max(1, lg.ncols);
    const int cols = (n + rows - 1) / rows;

    struct ItemMetric { float ascent, above, below; };
    std::vector<ItemMetric> im(n);
    for (int i = 0; i < n; ++i) {
        auto m = measure(entries[i].label, scale);
        im[i].ascent = m.ascent;
        im[i].above = std::max(m.ascent, hAbove);
        im[i].below = std::max(m.height - m.ascent, hBelow);
    }

    std::vector<float> colW(cols, 0.0f), colH(cols, 0.0f);
    for (int col = 0; col < cols; ++col)
        for (int r = 0; r < rows; ++r) {
            int idx = col * rows + r;
            if (idx >= n) break;
            colW[col] = std::max(colW[col],
                handleW + textGap + measure(entries[idx].label, scale).width);
            colH[col] += im[idx].above + im[idx].below + (r ? rowSep : 0.0f);
        }
    float contentW = colGap * (cols - 1);
    for (float w : colW) contentW += w;
    const float contentH = *std::ranges::max_element(colH);

    float titleW = 0.0f, titleH = 0.0f;
    const float titleScale = lg.titleFont.size * dpi / (72.0f * 16.0f);
    if (!lg.title.empty()) {
        auto m = measure(lg.title, titleScale);
        titleW = m.width;
        titleH = m.height;
    }
    // mpl mode="expand": widen the box; columns share the extra width.
    float boxW = pad * 2 + std::max(contentW, titleW);
    if (forceW > boxW) {
        const float extra = forceW - boxW;
        for (auto& w : colW) w += extra / std::max(1, cols);
        contentW += extra;
        boxW = forceW;
    }
    const float boxH = pad * 2 + titleH + rowSep + contentH;

    const float boxX = anchor.x - bx * boxW;
    const float boxY = anchor.y - (1.0f - by) * boxH;

    // mpl alignment: entry block + title align within the inner width.
    const float innerW = boxW - 2.0f * pad;
    const float alignF = lg.alignment == "left" ? 0.0f
                       : lg.alignment == "right" ? 1.0f : 0.5f;
    const float blockShift = std::max(0.0f, innerW - contentW) * alignF;

    if (lg.shadow) {
        const float so = fontPx * 0.25f;
        fillRect(c, {int32_t(boxX + so), int32_t(boxY + so),
                     uint32_t(boxW), uint32_t(boxH)},
                 Color::fromRgba8(0, 0, 0, 100));
    }
    if (lg.frameOn) {
        auto bg = lg.faceColor;
        bg.a *= lg.frameAlpha;
        fillRect(c, {int32_t(boxX), int32_t(boxY),
                     uint32_t(boxW), uint32_t(boxH)}, bg);
        strokeRect(c, {int32_t(boxX), int32_t(boxY),
                       uint32_t(boxW), uint32_t(boxH)},
                   penOf(lg.edgeColor, 1.0f));
    }

    float contentTop = boxY + pad;
    if (!lg.title.empty()) {
        auto m = measure(lg.title, titleScale);
        richText(c, lg.title,
                 boxX + pad + std::max(0.0f, innerW - m.width) * alignF,
                 contentTop + m.ascent, textColor, titleScale);
    }
    contentTop += titleH + rowSep;

    const auto labelColor = lg.labelColor.value_or(textColor);
    const float markerSize = fontPx * lg.markerScale;
    float firstAbove = 0.0f;
    for (int col = 0; col < cols; ++col)
        firstAbove = std::max(firstAbove, im[col * rows].above);

    auto handlePoints = [&](const LegendVecEntry& e) {
        if (e.points >= 0) return e.points;
        if (e.points == -2) return lg.numpoints;
        return e.marker == plot::LegendMarker::Circle ? lg.scatterpoints : 0;
    };
    auto disc = [&](float cx, float cy, float r, plot::Color col) {
        std::vector<Point2D> d;
        d.reserve(16);
        for (int k = 0; k < 16; ++k) {
            float a = k * 0.392699f;
            d.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        c.polygon(d, col);
    };

    for (int col = 0; col < cols; ++col) {
        float colX = boxX + pad + blockShift;
        for (int j = 0; j < col; ++j) colX += colW[j] + colGap;
        float baseline = contentTop + firstAbove;
        int prev = -1;
        for (int r = 0; r < rows; ++r) {
            const int i = col * rows + r;
            if (i >= n) break;
            if (prev >= 0)
                baseline += im[prev].below + rowSep + im[i].above;
            prev = i;
            const float midY = baseline - hAbove + hBoxH * 0.5f;
            const auto& e = entries[i];
            const int npts = handlePoints(e);

            // mpl markerfirst: [handle, text]; else [text, handle].
            const float hX = lg.markerFirst
                ? colX
                : colX + measure(e.label, scale).width + textGap;
            const float tX = lg.markerFirst
                ? colX + handleW + textGap
                : colX;

            if (e.marker == plot::LegendMarker::Line) {
                line(c, {hX, midY}, {hX + handleW, midY},
                     penOf(e.color, 2.0f));
                const float rad = markerSize * 0.3f;
                for (int p = 0; p < npts; ++p) {
                    const float fx = npts == 1 ? 0.5f
                        : float(p) / float(npts - 1);
                    disc(hX + fx * handleW, midY, rad, e.color);
                }
            } else if (e.marker == plot::LegendMarker::Circle) {
                const float rad = markerSize * (npts > 1 ? 0.35f : 0.5f);
                for (int p = 0; p < npts; ++p) {
                    const float fx = npts == 1 ? 0.5f
                        : float(p) / float(npts - 1);
                    disc(hX + handleW * 0.15f + fx * handleW * 0.7f,
                         midY, rad, e.color);
                }
            } else {
                const float sx = hX + (handleW - markerSize) / 2.0f;
                fillRect(c, {int32_t(sx), int32_t(midY - markerSize / 2.0f),
                             uint32_t(markerSize), uint32_t(markerSize)},
                         e.color);
            }
            richText(c, e.label, tX, baseline, labelColor, scale);
        }
    }
}

// ─── colorbar ───────────────────────────────────────────────────────────────

void VectorRenderer::emitColorbar(const plot::Axes& axes, Rect2D rect,
                                  VectorCanvas& c) {
    const auto& style = axes.style();
    if (!style.colorbar.visible) return;
    const float cbDpi =
        axes.figure() ? axes.figure()->dpi() : style.dpi;
    const float cbTextPx = style.fontSize * cbDpi / 72.0f;
    // Match the raster path: the mappable's valueRange (or the first
    // colormapped plot), then the z viewport as the 3D fallback.
    const auto& cbs = style.colorbar;
    float valueMin = 0.0f, valueMax = 1.0f;
    bool hasRange = false;
    if (cbs.mappable) {
        if (auto vr = cbs.mappable->valueRange(); vr && vr->valid()) {
            valueMin = vr->min; valueMax = vr->max; hasRange = true;
        }
    }
    if (!hasRange && cbs.explicitRange && cbs.explicitRange->valid()) {
        valueMin = cbs.explicitRange->min;
        valueMax = cbs.explicitRange->max;
        hasRange = true;
    }
    if (!hasRange)
        for (const auto& p : axes.plots())
            if (auto vr = p->valueRange(); vr && vr->valid()) {
                valueMin = vr->min; valueMax = vr->max;
                hasRange = true; break;
            }
    if (!hasRange) {
        const auto& vp = axes.viewport();
        if (vp.z.span() <= 0) return;
        valueMin = vp.z.min; valueMax = vp.z.max;
    }
    // Same geometry as the raster drawColorbar: strip sits in the right
    // `fraction` region of the pre-shrink parent box (or pad-fraction
    // outside unshrunk axes), narrowed to height/aspect.
    // mpl cax: the axes' own rect IS the colorbar region.
    plot::Rect2D region = cbs.caxMode ? rect : axes.colorbarRegion();
    float regionX, regionW;
    if (region.width > 0) {
        regionX = float(region.x);
        regionW = float(region.width);
    } else {
        regionX = float(rect.x) + float(rect.width) +
                  cbs.pad * float(rect.width);
        regionW = cbs.fraction * float(rect.width);
    }
    float stripX = cbs.padding > 0.0f
        ? float(rect.x) + float(rect.width) + cbs.padding
        : regionX;
    float stripH = float(rect.height) * cbs.shrink;
    float stripY = float(rect.y) + (float(rect.height) - stripH) * 0.5f;
    float stripW = cbs.width > 0.0f
        ? cbs.width
        : std::min(regionW, stripH / cbs.aspect);
    bool extMin = (cbs.extend == "min" || cbs.extend == "both");
    bool extMax = (cbs.extend == "max" || cbs.extend == "both");
    float extH = stripW * 0.6f;
    float bodyY0 = stripY + (extMax ? extH : 0.0f);
    float bodyY1 = stripY + stripH - (extMin ? extH : 0.0f);
    float bodyH = bodyY1 - bodyY0;

    // mpl: the mappable's cmap/norm win over the style's explicit
    // norm/colormap name.
    auto mappableNorm = cbs.mappable ? cbs.mappable->norm() : nullptr;
    const auto& cmap =
        cbs.cmapPtr ? *cbs.cmapPtr
        : (cbs.mappable && cbs.mappable->cmap() ? *cbs.mappable->cmap()
           : plot::Colormap::byName(cbs.colormap));
    const plot::Normalize* effNorm =
        cbs.norm ? cbs.norm.get() : mappableNorm.get();
    auto sampleAt = [&](float t) {
        plot::Color c = effNorm
            ? cmap.sample((*effNorm)(valueMin + t * (valueMax - valueMin)))
            : cmap.sample(t);
        c.a *= cbs.alpha; // mpl fig.colorbar(alpha=)
        return c;
    };

    constexpr uint32_t kSegs = 64;
    if (cbs.orientation == "horizontal") {
        float regionY = region.height > 0 ? float(region.y)
            : float(rect.y) + float(rect.height) +
                  cbs.pad * float(rect.height);
        float regionH = region.height > 0 ? float(region.height)
            : cbs.fraction * float(rect.height);
        float stripW = float(rect.width) * cbs.shrink;
        float stripX = float(rect.x) + (float(rect.width) - stripW) * 0.5f;
        float stripH = cbs.width > 0.0f
            ? cbs.width : std::min(regionH, stripW / cbs.aspect);
        float stripY = cbs.padding > 0.0f
            ? float(rect.y) + float(rect.height) + cbs.padding
            : regionY;
        float extW = stripH * 0.6f;
        float bodyX0 = stripX + (extMin ? extW : 0.0f);
        float bodyX1 = stripX + stripW - (extMax ? extW : 0.0f);
        float bodyW = bodyX1 - bodyX0;

        float segW = bodyW / kSegs;
        for (uint32_t i = 0; i < kSegs; ++i) {
            float t = float(i) / float(kSegs - 1);
            fillRect(c, {int32_t(bodyX0 + i * segW - 1), int32_t(stripY),
                         uint32_t(segW) + 2, uint32_t(stripH)},
                     sampleAt(t));
        }
        if (extMin) {
            Point2D tri[3] = {{bodyX0, stripY}, {bodyX0, stripY + stripH},
                              {bodyX0 - extW, stripY + stripH / 2.0f}};
            c.polygon(tri, sampleAt(0.0f));
        }
        if (extMax) {
            Point2D tri[3] = {{bodyX1, stripY}, {bodyX1, stripY + stripH},
                              {bodyX1 + extW, stripY + stripH / 2.0f}};
            c.polygon(tri, sampleAt(1.0f));
        }
        strokeRect(c, {int32_t(bodyX0), int32_t(stripY),
                       uint32_t(bodyW), uint32_t(stripH)},
                   penOf(cbs.edgeColor, 1.0f));

        auto cbt = colorbarTicks(cbs.ticks, cbs.tickLabels,
                                 cbs.minorTicksOn, effNorm,
                                 valueMin, valueMax, cbs.format);
        auto hToFrac = [&](float v) {
            return effNorm ? (*effNorm)(v)
                           : (v - valueMin) / (valueMax - valueMin);
        };
        auto tickPen = penOf(cbs.edgeColor, 1.0f);
        for (size_t ti = 0; ti < cbt.majors.size(); ++ti) {
            float t = hToFrac(cbt.majors[ti]);
            if (t < 0.0f || t > 1.0f) continue;   // mpl clips to the bar
            float x = bodyX0 + t * bodyW;
            plot::Point2D tickPts[2] = {
                {x, stripY + stripH}, {x, stripY + stripH + 4.0f}};
            c.polyline(std::span{tickPts, 2}, tickPen);
            richText(c, cbt.labels[ti],
                     bodyX0 + t * bodyW - 8.0f, stripY + stripH + 18.0f,
                     cbs.labelColor, cbTextPx / 16.0f);
        }
        for (float mv : cbt.minors) {
            float t = hToFrac(mv);
            if (t <= 0.0f || t >= 1.0f) continue;
            float x = bodyX0 + t * bodyW;
            plot::Point2D mTick[2] = {
                {x, stripY + stripH}, {x, stripY + stripH + 2.0f}};
            c.polyline(std::span{mTick, 2}, tickPen);
        }
        return;
    }

    float segH = bodyH / kSegs;
    for (uint32_t i = 0; i < kSegs; ++i) {
        float t = float(i) / float(kSegs - 1);
        // t=0 (valueMin) at the bottom (larger y).
        fillRect(c, {int32_t(stripX), int32_t(bodyY1 - (i + 1) * segH - 1),
                     uint32_t(stripW), uint32_t(segH) + 2}, sampleAt(t));
    }
    if (extMin) {
        Point2D tri[3] = {{stripX, bodyY1}, {stripX + stripW, bodyY1},
                          {stripX + stripW / 2.0f, bodyY1 + extH}};
        c.polygon(tri, sampleAt(0.0f));
    }
    if (extMax) {
        Point2D tri[3] = {{stripX, bodyY0}, {stripX + stripW, bodyY0},
                          {stripX + stripW / 2.0f, bodyY0 - extH}};
        c.polygon(tri, sampleAt(1.0f));
    }
    // Edge outline.
    strokeRect(c, {int32_t(stripX), int32_t(stripY),
                   uint32_t(stripW), uint32_t(stripH)},
               penOf(cbs.edgeColor, 1.0f));

    // Tick labels to the right of the strip; locator-appropriate ticks
    // for log/symlog norms (colorbarTicks), mpl clip-to-bar behavior.
    auto cbt = colorbarTicks(cbs.ticks, cbs.tickLabels, cbs.minorTicksOn,
                             effNorm, valueMin, valueMax, cbs.format);
    auto toFrac = [&](float v) {
        return effNorm ? (*effNorm)(v)
                       : (v - valueMin) / (valueMax - valueMin);
    };
    auto tickPen = penOf(cbs.edgeColor, 1.0f);
    for (size_t ti = 0; ti < cbt.majors.size(); ++ti) {
        float t = toFrac(cbt.majors[ti]);
        if (t < 0.0f || t > 1.0f) continue;
        float y = bodyY1 - t * bodyH;
        plot::Point2D tickPts[2] = {
            {stripX + stripW, y}, {stripX + stripW + 4.0f, y}};
        c.polyline(std::span{tickPts, 2}, tickPen);
        richText(c, cbt.labels[ti], stripX + stripW + 8.0f, y + 6.0f,
                 cbs.labelColor, cbTextPx / 16.0f);
    }
    for (float mv : cbt.minors) {
        float t = toFrac(mv);
        if (t <= 0.0f || t >= 1.0f) continue;
        float y = bodyY1 - t * bodyH;
        plot::Point2D mTick[2] = {
            {stripX + stripW, y}, {stripX + stripW + 2.0f, y}};
        c.polyline(std::span{mTick, 2}, tickPen);
    }
}

} // namespace volcano::render
