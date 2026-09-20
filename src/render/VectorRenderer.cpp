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
    for (const auto& p : fig.placements())
        emitAxes(*p.axes, p.axes->rect, canvas);
    for (const auto& sf : fig.subfigs())
        render(*sf.figure, canvas);

    // Figure suptitle at top center.
    const auto& t = fig.style().title;
    if (!t.text.empty()) {
        float scale = t.font.size / 12.0f;
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
}

void VectorRenderer::emitAxes(const plot::Axes& axes, Rect2D rect,
                              VectorCanvas& c) {
    const auto& style = axes.style();
    // Axes facecolor patch.
    if (style.faceColor.a > 0)
        fillRect(c, rect, style.faceColor);

    emitGrid(axes, rect, c);
    emitPlots(axes, rect, c);
    emitSpinesTicks(axes, rect, c);
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
                                style.fontSize, style.dpi, !vert);
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
        if (p->canEmitVector() && !p->rasterized) {
            const_cast<plot::IPlot*>(p)->emitVector(c, axes, rect);
            ++i;
            continue;
        }
        // Contiguous run of rasterized/non-native layers → one image.
        std::vector<const plot::IPlot*> run;
        while (i < order.size() &&
               !(order[i]->canEmitVector() && !order[i]->rasterized))
            run.push_back(order[i++]);
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
    if (!style.xAxis.visible && !style.yAxis.visible) return;

    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    Pen sp = penOf(style.xAxis.color, std::max(style.xAxis.lineWidth, 1.0f));
    const auto& spv = axes.spines();
    if (spv.bottom) line(c, {x0, y1}, {x1, y1}, sp);
    if (spv.top)    line(c, {x0, y0}, {x1, y0}, sp);
    if (spv.left)   line(c, {x0, y0}, {x0, y1}, sp);
    if (spv.right)  line(c, {x1, y0}, {x1, y1}, sp);

    const auto& vp = axes.viewport();
    auto tickMarks = [&](const plot::AxisStyle& as,
                         const plot::AxisScale& scale,
                         float lo, float hi, bool yAxis, bool farSide) {
        const auto& tc = as.ticks;
        float inF = tickInFrac(tc);
        auto emitSet = [&](std::span<const float> ticks, float sizePt,
                           float widthPt) {
            Pen p = penOf(as.color, std::max(widthPt * kPtToPx, 1.0f));
            float len = sizePt * kPtToPx;
            for (float t : ticks) {
                auto f = yAxis ? axes.dataToFraction({0.0f, t})
                               : axes.dataToFraction({t, 0.0f});
                if (yAxis) {
                    float py = y1 - f.y * float(rect.height);
                    if (py < y0 || py > y1) continue;
                    float edge = farSide ? x1 : x0;
                    float dir = farSide ? 1.0f : -1.0f;
                    // `inF` of the length goes inside, rest outside.
                    line(c, {edge + dir * len * (1.0f - inF), py},
                         {edge - dir * len * inF, py}, p);
                } else {
                    float px = x0 + f.x * float(rect.width);
                    if (px < x0 || px > x1) continue;
                    float edge = farSide ? y0 : y1;
                    float dir = farSide ? -1.0f : 1.0f;
                    line(c, {px, edge + dir * len * (1.0f - inF)},
                         {px, edge - dir * len * inF}, p);
                }
            }
        };
        float axisLen = yAxis ? float(rect.height) : float(rect.width);
        auto majors = axisTicks(tc, scale, lo, hi, axisLen,
                                style.fontSize, style.dpi, yAxis);
        emitSet(majors, tc.majorSize, tc.majorWidth);
        auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
        emitSet(minors, tc.minorSize, tc.minorWidth);
    };
    if (style.xAxis.visible)
        tickMarks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                  false, axes.xTicksTop());
    if (style.yAxis.visible)
        tickMarks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                  true, axes.yTicksRight());
}

// ─── labels, tick labels, title ─────────────────────────────────────────────

void VectorRenderer::emitLabels(const plot::Axes& axes, Rect2D rect,
                                VectorCanvas& c) {
    const auto& style = axes.style();
    const auto& vp = axes.viewport();
    const float scale = 1.0f;
    const Color labelColor = style.textColor;
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    constexpr float kTickLength = 4.0f;

    if (style.xAxis.visible && !style.xAxis.label.empty()) {
        auto m = measure(style.xAxis.label, scale);
        richText(c, style.xAxis.label,
                 rect.x + rect.width / 2.0f - m.width / 2.0f,
                 y1 + kTickLength +
                     style.xAxis.ticks.majorPad * kPtToPx + 16.0f +
                     style.xAxis.labelPad * kPtToPx + m.ascent,
                 labelColor, scale, style.xAxis.labelFont.rotation,
                 plot::HAlign::Center);
    }
    if (style.yAxis.visible && !style.yAxis.label.empty()) {
        auto m = measure(style.yAxis.label, scale);
        float oy = y0 + (y1 - y0) / 2.0f + m.width / 2.0f;
        float centerPos = x0 - kTickLength -
                          style.yAxis.ticks.majorPad * kPtToPx - 40.0f -
                          style.yAxis.labelPad * kPtToPx;
        float ox = centerPos - m.height / 2.0f + m.ascent;
        constexpr float kRotMinus90 = -1.5707963267948966f;
        richText(c, style.yAxis.label, ox, oy, labelColor, scale,
                 kRotMinus90 + style.yAxis.labelFont.rotation,
                 plot::HAlign::Center);
    }
    if (!style.title.text.empty()) {
        auto m = measure(style.title.text, scale);
        float tx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        float ty = y0 - style.title.pad * kPtToPx - m.height + m.ascent;
        richText(c, style.title.text, tx, ty,
                 style.title.color, scale, style.title.font.rotation,
                 plot::HAlign::Center);
        // Faux bold: second pass offset ~0.6px (mirrors raster).
        if (style.title.weight == "bold" ||
            style.title.font.weight == "bold")
            richText(c, style.title.text, tx + 0.6f, ty,
                     style.title.color, scale, style.title.font.rotation,
                     plot::HAlign::Center);
    }

    // X tick labels.
    if (style.xAxis.visible) {
        const auto& tc = style.xAxis.ticks;
        auto xTicks = axisTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                float(rect.width), style.fontSize,
                                style.dpi, false);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        auto* fmt = axisFormatter(tc, xTicks, style, axes.xscale(),
                                  vp.x.min, vp.x.max,
                                  defaultFmt, strFmt, logFmt);
        bool top = axes.xTicksTop();
        float edge = top ? y0 : y1;
        float d = top ? -1.0f : 1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        const float kTickSpacing = tc.majorPad * kPtToPx;
        int i = 0;
        for (float tick : xTicks) {
            float px = x0 + axes.dataToFraction({tick, 0.0f}).x * rect.width;
            if (px < x0 || px > x1) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measure(label, scale);
            float rot = style.xAxis.tickFont.rotation;
            float baseY = top ? tickEnd - kTickSpacing - m.height + m.ascent
                              : tickEnd + kTickSpacing + m.ascent;
            float x, y = baseY;
            if (rot != 0.0f) {
                // Rotated: anchor the baseline's right end at the tick
                // (mpl ha='right' for rotated xticklabels).
                float cosR = std::cos(rot), sinR = std::sin(rot);
                x = px - m.width * cosR;
                y = baseY - m.width * sinR;
            } else {
                x = px - m.width * 0.5f;
            }
            richText(c, label, x, y, labelColor, scale, rot);
        }
        auto off = fmt->offsetText();
        if (!off.empty()) {
            auto m = measure(off, scale * 0.8f);
            float x = x1 - m.width * 0.5f;
            float y = top ? y0 - outLen - kTickSpacing - m.height + m.ascent
                          : tickEnd + kTickSpacing + m.height + m.ascent * 0.2f;
            richText(c, off, x, y, labelColor, scale * 0.8f);
        }
        if (logMinorLabels(tc, axes.xscale())) {
            auto minor = axisMinorTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                        xTicks);
            plot::LogFormatterSciNotation defMinorFmt;
            plot::Formatter* mfmt = tc.minorFormatter
                                        ? tc.minorFormatter.get()
                                        : &defMinorFmt;
            mfmt->setViewInterval(vp.x.min, vp.x.max);
            mfmt->setLocs(minor);
            float mEnd = edge + d * tc.minorSize * kPtToPx *
                            (1.0f - tickInFrac(tc));
            float mGap = tc.minorPad * kPtToPx;
            int mi = 0;
            for (float t : minor) {
                float px = x0 + axes.dataToFraction({t, 0.0f}).x * rect.width;
                if (px < x0 || px > x1) { ++mi; continue; }
                auto label = mfmt->format(t, mi++);
                if (label.empty()) continue;
                auto m = measure(label, scale);
                richText(c, label, px - m.width * 0.5f,
                         mEnd + mGap + m.ascent, labelColor, scale);
            }
        }
    }

    // Y tick labels.
    if (style.yAxis.visible) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                float(rect.height), style.fontSize,
                                style.dpi, true);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        auto* fmt = axisFormatter(tc, yTicks, style, axes.yscale(),
                                  vp.y.min, vp.y.max,
                                  defaultFmt, strFmt, logFmt);
        bool right = axes.yTicksRight();
        float edge = right ? x1 : x0;
        float d = right ? 1.0f : -1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        const float kTickSpacing = tc.majorPad * kPtToPx;
        int i = 0;
        for (float tick : yTicks) {
            float py = y1 - axes.dataToFraction({0.0f, tick}).y * rect.height;
            if (py < y0 || py > y1) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measure(label, scale);
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
            richText(c, label, x, y, labelColor, scale, rot);
        }
        auto off = fmt->offsetText();
        if (!off.empty()) {
            auto m = measure(off, scale * 0.8f);
            float x = right ? x1 - m.width : x0;
            richText(c, off, x, y0 - kTickSpacing, labelColor, scale * 0.8f);
        }
        if (logMinorLabels(tc, axes.yscale())) {
            auto minor = axisMinorTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                        yTicks);
            plot::LogFormatterSciNotation defMinorFmt;
            plot::Formatter* mfmt = tc.minorFormatter
                                        ? tc.minorFormatter.get()
                                        : &defMinorFmt;
            mfmt->setViewInterval(vp.y.min, vp.y.max);
            mfmt->setLocs(minor);
            float mEnd = edge + d * tc.minorSize * kPtToPx *
                            (1.0f - tickInFrac(tc));
            float mGap = tc.minorPad * kPtToPx;
            int mi = 0;
            for (float t : minor) {
                float py = y1 - axes.dataToFraction({0.0f, t}).y * rect.height;
                if (py < y0 || py > y1) { ++mi; continue; }
                auto label = mfmt->format(t, mi++);
                if (label.empty()) continue;
                auto m = measure(label, scale);
                richText(c, label,
                         right ? mEnd + mGap : mEnd - mGap - m.width,
                         py + m.ascent - m.height * 0.5f, labelColor, scale);
            }
        }

        // --- Secondary y axis (mpl secondary_yaxis): right-side tick
        // labels in transformed units via the inverse map.
        if (auto sec = axes.secondaryY()) {
            float slo = sec->forward(vp.y.min), shi = sec->forward(vp.y.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.height), style.fontSize,
                                    style.dpi, true);
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
                auto m = measure(label, scale);
                richText(c, label, rEdge + outLen + kTickSpacing,
                         py + m.ascent - m.height * 0.5f, labelColor, scale);
            }
        }
    }

    // --- Secondary x axis (mpl secondary_xaxis): top-side tick labels.
    if (auto sec = axes.secondaryX()) {
        const auto& tc = style.xAxis.ticks;
        float slo = sec->forward(vp.x.min), shi = sec->forward(vp.x.max);
        auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                float(rect.width), style.fontSize,
                                style.dpi, false);
        plot::ScalarFormatter sFmt;
        sFmt.setLocs(sTicks);
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * kPtToPx;
        for (float st : sTicks) {
            float vv = sec->inverse(st);
            float px = x0 +
                       axes.dataToFraction({vv, 0.0f}).x * rect.width;
            if (px < x0 || px > x1) continue;
            auto label = sFmt.format(st, 0);
            if (label.empty()) continue;
            auto m = measure(label, scale);
            richText(c, label, px - m.width * 0.5f,
                     y0 - outLen - kTickSpacing - m.height + m.ascent,
                     labelColor, scale);
        }
    }
}

// ─── annotations ────────────────────────────────────────────────────────────

void VectorRenderer::emitAnnotations(const plot::Axes& axes, Rect2D rect,
                                     VectorCanvas& c) {
    plot::Extent2D figExtent = extent_;
    float dpi = axes.style().dpi;
    auto clipped = [&](const auto& fn, bool clipOn) {
        if (clipOn) c.pushClip(rect);
        fn();
        if (clipOn) c.popClip();
    };

    for (const auto& t : axes.texts()) {
        if (t.text.empty()) continue;
        auto pos = plot::toDisplay(t.x, t.y, t.coords, rect, figExtent, axes,
                                   dpi, t.xyOffsetX, t.xyOffsetY);
        auto m = measure(t.text, t.fontSize);
        {
            auto al = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            t.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                         uint32_t(m.width), uint32_t(m.height)};
        }
        clipped([&] {
            if (t.bboxFaceColor.a > 0.0f) {
                auto al = plot::alignText(pos, t.halign, t.valign,
                                          m.width, m.height, m.ascent);
                float pad = t.bboxPadding;
                fillRect(c, {int32_t(al.x - pad),
                             int32_t(al.y - m.ascent - pad),
                             uint32_t(m.width + 2 * pad),
                             uint32_t(m.height + 2 * pad)}, t.bboxFaceColor);
                if (t.bboxEdgeColor.a > 0.0f)
                    strokeRect(c, {int32_t(al.x - pad),
                                   int32_t(al.y - m.ascent - pad),
                                   uint32_t(m.width + 2 * pad),
                                   uint32_t(m.height + 2 * pad)},
                               penOf(t.bboxEdgeColor, 1.0f));
            }
            auto dp = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            richText(c, t.text, dp.x, dp.y, t.color, t.fontSize, t.rotation,
                     t.halign);
        }, t.clipOn);
    }

    for (const auto& a : axes.annotations()) {
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
                auto m = measure(a.text, a.fontSize);
                {
                    auto al = plot::alignText(textPos, a.halign, a.valign,
                                              m.width, m.height, m.ascent);
                    a.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                                 uint32_t(m.width), uint32_t(m.height)};
                }
                if (a.bboxFaceColor.a > 0.0f) {
                    auto al = plot::alignText(textPos, a.halign, a.valign,
                                              m.width, m.height, m.ascent);
                    float pad = a.bboxPadding;
                    plot::Rect2D brect{int32_t(al.x - pad),
                                       int32_t(al.y - m.ascent - pad),
                                       uint32_t(m.width + 2 * pad),
                                       uint32_t(m.height + 2 * pad)};
                    if (a.boxStyle) {
                        auto bs = *a.boxStyle;
                        bs.mutationSize *= a.fontSize * 16.0f;
                        auto path = plot::boxStylePath(float(brect.x),
                                                       float(brect.y),
                                                       float(brect.width),
                                                       float(brect.height),
                                                       bs);
                        for (auto& sp : path.toPolylines(24)) {
                            if (a.bboxFaceColor.a > 0.0f)
                                c.polygon(sp.points, a.bboxFaceColor);
                            if (a.bboxEdgeColor.a > 0.0f) {
                                auto ring = sp.points;
                                if (sp.closed && !ring.empty())
                                    ring.push_back(ring.front());
                                c.polyline(ring,
                                           penOf(a.bboxEdgeColor, 1.0f));
                            }
                        }
                    } else {
                        fillRect(c, brect, a.bboxFaceColor);
                        if (a.bboxEdgeColor.a > 0.0f)
                            strokeRect(c, brect, penOf(a.bboxEdgeColor, 1.0f));
                    }
                }
                auto dp = plot::alignText(textPos, a.halign, a.valign,
                                          m.width, m.height, m.ascent);
                richText(c, a.text, dp.x, dp.y, a.color, a.fontSize, 0.0f,
                         a.halign);
            }
        }, a.clipOn);
    }
}

// ─── legend ─────────────────────────────────────────────────────────────────


void VectorRenderer::emitSizeBars(const plot::Axes& axes, Rect2D rect,
                                  VectorCanvas& c) {
    if (axes.sizeBars().empty()) return;
    const float dpi = axes.style().dpi;
    auto measureFn = [this](std::string_view t, float scale) {
        auto m = measure(t, scale);
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& sb : axes.sizeBars()) {
        auto L = plot::layoutSizeBar(sb, axes, rect, extent_, dpi,
                                     measureFn);
        if (!L.valid) continue;
        auto toI = [](plot::Rect2Df r) {
            return Rect2D{int32_t(std::lround(r.x)),
                          int32_t(std::lround(r.y)),
                          uint32_t(std::lround(r.w)),
                          uint32_t(std::lround(r.h))};
        };
        if (sb.frameon)
            fillRect(c, toI(L.box), sb.frameFaceColor);
        if (L.fill)
            fillRect(c, toI(L.bar), sb.color);
        else
            strokeRect(c, toI(L.bar), penOf(sb.color, 1.0f));
        if (!sb.label.empty())
            richText(c, sb.label, L.labelBaseline.x, L.labelBaseline.y,
                     sb.color, sb.fontSize);
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
    if (axes.anchoredTexts().empty()) return;
    auto measureFn = [this](std::string_view t, float scale) {
        auto m = measure(t, scale);
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& at : axes.anchoredTexts()) {
        auto L = plot::layoutAnchoredText(at, axes, rect, measureFn);
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
                     L.lineBaselines[i].y, at.color, at.fontSize);
            ++i;
            if (nl == std::string_view::npos) break;
            start = nl + 1;
        }
    }
}

void VectorRenderer::emitLegend(const plot::Axes& axes, Rect2D rect,
                                VectorCanvas& c) {
    const auto& style = axes.style();
    const auto& lg = style.legend;
    if (!lg.visible) return;

    struct Entry { std::string label; Color color; plot::LegendMarker marker; };
    std::vector<Entry> entries;
    for (auto& plot : axes.plots()) {
        if (auto it = lg.handlerMap.find(std::type_index(typeid(*plot)));
            it != lg.handlerMap.end()) {
            for (auto& h : it->second(*plot))
                entries.push_back({std::move(h.label), h.color, h.marker});
            continue;
        }
        for (auto& h : plot->legendEntries())
            entries.push_back({std::move(h.label), h.color, h.marker});
    }
    if (entries.empty()) return;

    const float scale = lg.font.size / 12.0f;
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
    const float titleScale = lg.titleFont.size / 12.0f;
    if (!lg.title.empty()) {
        auto m = measure(lg.title, titleScale);
        titleW = m.width;
        titleH = m.height;
    }
    const float boxW = pad * 2 + std::max(contentW, titleW);
    const float boxH = pad * 2 + titleH + rowSep + contentH;

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
    float px, py;
    if (hasAnchor && lg.anchorSpace == plot::CoordSystem::Figure) {
        px = afx * extent_.width;
        py = (1.0f - afy) * extent_.height;
    } else {
        px = rect.x + afx * rect.width;
        py = rect.y + (1.0f - afy) * rect.height;
    }
    if (!hasAnchor) {
        const float m = lg.borderAxesPad * fontPx;
        if (la.bx > 0.5f) px -= m; else if (la.bx < 0.5f) px += m;
        if (la.by > 0.5f) py += m; else if (la.by < 0.5f) py -= m;
    }
    const float boxX = px - la.bx * boxW;
    const float boxY = py - (1.0f - la.by) * boxH;

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
        richText(c, lg.title, boxX + boxW / 2.0f - m.width / 2.0f,
                 contentTop + m.ascent, style.textColor, titleScale);
    }
    contentTop += titleH + rowSep;

    const auto labelColor = lg.labelColor.value_or(style.textColor);
    float firstAbove = 0.0f;
    for (int col = 0; col < cols; ++col)
        firstAbove = std::max(firstAbove, im[col * rows].above);

    for (int col = 0; col < cols; ++col) {
        float colX = boxX + pad;
        for (int j = 0; j < col; ++j) colX += colW[j] + colGap;
        const float markerSize = fontPx;
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

            if (e.marker == plot::LegendMarker::Line) {
                line(c, {colX, midY}, {colX + handleW, midY},
                     penOf(e.color, 2.0f));
            } else if (e.marker == plot::LegendMarker::Circle) {
                const float cx = colX + handleW / 2.0f, rad = markerSize / 2.0f;
                std::vector<Point2D> disc;
                for (int k = 0; k < 16; ++k) {
                    float a = k * 0.392699f;
                    disc.push_back({cx + rad * std::cos(a),
                                    midY + rad * std::sin(a)});
                }
                c.polygon(disc, e.color);
            } else {
                const float sx = colX + (handleW - markerSize) / 2.0f;
                fillRect(c, {int32_t(sx), int32_t(midY - markerSize / 2.0f),
                             uint32_t(markerSize), uint32_t(markerSize)},
                         e.color);
            }
            richText(c, e.label, colX + handleW + textGap, baseline,
                     labelColor, scale);
        }
    }
}

// ─── colorbar ───────────────────────────────────────────────────────────────

void VectorRenderer::emitColorbar(const plot::Axes& axes, Rect2D rect,
                                  VectorCanvas& c) {
    const auto& style = axes.style();
    if (!style.colorbar.visible) return;
    const auto& vp = axes.viewport();
    if (vp.z.span() <= 0) return;
    float valueMin = vp.z.min, valueMax = vp.z.max;

    const auto& cbs = style.colorbar;
    // Same geometry as the raster drawColorbar: strip sits in the right
    // `fraction` region of the pre-shrink parent box (or pad-fraction
    // outside unshrunk axes), narrowed to height/aspect.
    plot::Rect2D region = axes.colorbarRegion();
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

    const auto& cmap = plot::Colormap::byName(cbs.colormap);
    auto sampleAt = [&](float t) {
        if (cbs.norm)
            return cmap.sample((*cbs.norm)(valueMin + t * (valueMax - valueMin)));
        return cmap.sample(t);
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

        auto hticks = autoTicks(valueMin, valueMax, 8);
        float hStep = autoTickStep(valueMin, valueMax, 8);
        for (float tick : hticks) {
            float t = cbs.norm ? (*cbs.norm)(tick)
                               : (tick - valueMin) / (valueMax - valueMin);
            c.text({bodyX0 + t * bodyW - 8.0f, stripY + stripH + 18.0f},
                   formatTick(tick, hStep), 16.0f, cbs.labelColor);
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

    // Tick labels to the right of the strip.
    auto cbTicks = autoTicks(valueMin, valueMax, 8);
    float cbStep = autoTickStep(valueMin, valueMax, 8);
    for (float tick : cbTicks) {
        float t = (tick - valueMin) / (valueMax - valueMin);
        float y = bodyY1 - t * bodyH;
        auto label = formatTick(tick, cbStep);
        c.text({stripX + stripW + 8.0f, y + 6.0f}, label, 16.0f,
               cbs.labelColor);
    }
}

} // namespace volcano::render
