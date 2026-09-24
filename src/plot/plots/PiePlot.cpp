// volcano/plot/plots/PiePlot.cpp
#include "volcano/plot/plots/PiePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/backend/Backend.hpp"
#include <cmath>
#include <cstdio>
namespace volcano::plot {
namespace {

constexpr float PI = 3.14159265358979323846f;

/// mpl pie geometry shared by the raster label pass and emitVector.
/// Pie data units: the axes view spans ±1.25, so `unitPx` pixels per
/// data unit = 0.8 × the axes half-extent (min of w/h for a circle).
struct PieGeom {
    float unitPx = 0.0f;    // px per pie data unit
    float rcx = 0.0f, rcy = 0.0f; // axes rect center in px
    float denom = 1.0f;     // normalize ? sum : 1
    float dir = 1.0f;       // +1 CCW / -1 CW
    float start = 0.0f;     // startangle in circle fractions
    bool ok = false;        // false when the values sum to <= 0

    explicit PieGeom(const PieData& d, Rect2D rect) {
        float halfW = float(rect.width) * 0.5f;
        float halfH = float(rect.height) * 0.5f;
        unitPx = 0.8f * std::min(halfW, halfH);
        rcx = float(rect.x) + halfW;
        rcy = float(rect.y) + halfH;
        float total = 0;
        for (auto v : d.values) total += v;
        ok = total > 0 && !d.values.empty();
        denom = d.normalize ? total : 1.0f;
        dir = d.counterclock ? 1.0f : -1.0f;
        start = d.startAngle / 360.0f;
    }

    /// pie-data-unit point → figure px (Y-up data → Y-down screen).
    [[nodiscard]] Point2D toPx(Point2D p) const {
        return {rcx + p.x * unitPx, rcy - p.y * unitPx};
    }
    float explodeAt(const PieData& d, size_t i) const {
        if (d.explode.empty()) return 0.0f;
        if (d.explode.size() == 1) return d.explode[0];
        return i < d.explode.size() ? d.explode[i] : 0.0f;
    }
    /// Fraction of the circle occupied by wedge i.
    float frac(const PieData& d, size_t i) const {
        return d.values[i] / denom;
    }
};

/// printf-subset for mpl `autopct`: float conversion directives are fed
/// `v`, `%%` emits a literal %. Non-float conversions are emitted
/// verbatim (mpl would raise TypeError).
std::string formatAutopct(std::string_view fmt, double v) {
    std::string out;
    for (size_t i = 0; i < fmt.size();) {
        if (fmt[i] != '%') { out += fmt[i++]; continue; }
        size_t j = i + 1;
        if (j < fmt.size() && fmt[j] == '%') {
            out += '%';
            i = j + 1;
            continue;
        }
        while (j < fmt.size() &&
               std::string_view("-+ #0.0123456789lLh")
                       .find(fmt[j]) != std::string_view::npos)
            ++j;
        if (j >= fmt.size()) { out.append(fmt.substr(i)); break; }
        char conv = fmt[j++];
        if (std::string_view("fFeEgGaA").find(conv) !=
                std::string_view::npos) {
            char buf[128];
            std::string spec(fmt.substr(i, j - i));
            int n = std::snprintf(buf, sizeof buf, spec.c_str(), v);
            if (n > 0)
                out.append(buf, size_t(std::min<int>(n, sizeof(buf) - 1)));
        } else {
            out.append(fmt.substr(i, j - i));
        }
        i = j;
    }
    return out;
}

std::string autopctLabel(const PieData& d, float frac) {
    if (d.autopctFn) return d.autopctFn(100.0 * double(frac));
    return formatAutopct(d.autopct, 100.0 * double(frac));
}

} // namespace

void PiePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void PiePlot::draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes&, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect = clipRectVk(rect, r.backend().extent());
    renderer_.draw(cmd, vrect);

    PieGeom g(data_, rect);
    if (!g.ok) return;

    auto& spine = r.spineRenderer();
    vk::Rect2D fullRect{{0, 0}, r.backend().extent()};
    auto& text = r.textRenderer();

    // White edge lines between slices (mpl wedgeprops edgecolor) at each
    // wedge start, from the (exploded) wedge center to the outer rim.
    float theta1 = g.start;
    for (size_t i = 0; i < data_.values.size(); ++i) {
        float theta2 = theta1 + g.dir * g.frac(data_, i);
        float thetam = PI * (theta1 + theta2);
        float expl = g.explodeAt(data_, i);
        Point2D wc{data_.center.x + expl * std::cos(thetam),
                   data_.center.y + expl * std::sin(thetam)};
        float a0 = 2.0f * PI * theta1;
        Point2D e{wc.x + data_.radius * std::cos(a0),
                  wc.y + data_.radius * std::sin(a0)};
        Point2D edgePts[2] = {g.toPx(wc), g.toPx(e)};
        spine.drawLineStrip(cmd, fullRect, r.backend().extent(),
                            edgePts, Color::white(), 1.0f);
        theta1 = theta2;
    }

    // Category labels (outside) + autopct labels (inside) — mpl pie().
    const bool labelsOn = !std::isnan(data_.labelDistance);
    const bool pctOn = data_.autopctFn || !data_.autopct.empty();
    if (!labelsOn && !pctOn) return;

    theta1 = g.start;
    for (size_t i = 0; i < data_.values.size(); ++i) {
        float frac = g.frac(data_, i);
        float theta2 = theta1 + g.dir * frac;
        float thetam = PI * (theta1 + theta2);
        float expl = g.explodeAt(data_, i);
        Point2D wc{data_.center.x + expl * std::cos(thetam),
                   data_.center.y + expl * std::sin(thetam)};

        if (labelsOn && i < data_.labels.size() &&
            !data_.labels[i].empty()) {
            // xt = x + labeldistance*radius*cos(thetam) (data units)
            float xt = wc.x + data_.labelDistance * data_.radius *
                           std::cos(thetam);
            float yt = wc.y + data_.labelDistance * data_.radius *
                           std::sin(thetam);
            auto lm = text.measureText(data_.labels[i], 1.0f);
            Point2D lp = g.toPx({xt, yt});
            if (data_.rotatelabels) {
                // mpl: va = 'bottom' if yt>0 else 'top', ha stays
                // 'left'/'right', rotation = deg(thetam) (+180 on the
                // left half). mpl rotation is CCW-positive in display
                // space; ours is clockwise-positive in Y-down px, so
                // the angle is negated. Rotation pivots on the anchor.
                float rot = -thetam + (xt > 0 ? 0.0f : PI);
                float dx = (xt > 0) ? 0.0f : -lm.width;
                float dy = (yt > 0) ? -(lm.height - lm.ascent)
                                    : lm.ascent;
                float cs = std::cos(rot), sn = std::sin(rot);
                float bx = lp.x + dx * cs - dy * sn;
                float by = lp.y + dx * sn + dy * cs;
                text.draw(cmd, fullRect, data_.labels[i], bx, by,
                          Color::black(), 1.0f, rot);
            } else {
                // mpl: ha = 'left' if xt>0 else 'right', va='center'.
                float drawX = xt > 0 ? lp.x : lp.x - lm.width;
                float drawY = lp.y + lm.ascent - lm.height * 0.5f;
                text.draw(cmd, fullRect, data_.labels[i], drawX, drawY,
                          Color::black(), 1.0f);
            }
        }

        if (pctOn) {
            // mpl: ha/va 'center', no rotation.
            float xt = wc.x + data_.pctDistance * data_.radius *
                           std::cos(thetam);
            float yt = wc.y + data_.pctDistance * data_.radius *
                           std::sin(thetam);
            std::string s = autopctLabel(data_, frac);
            auto pm = text.measureText(s, 1.0f);
            Point2D pp = g.toPx({xt, yt});
            text.draw(cmd, fullRect, s,
                      pp.x - pm.width * 0.5f,
                      pp.y + pm.ascent - pm.height * 0.5f,
                      Color::black(), 1.0f);
        }
        theta1 = theta2;
    }
}

void PiePlot::emitVector(render::VectorCanvas& c, const Axes&, Rect2D rect) {
    PieGeom g(data_, rect);
    if (!g.ok) return;

    constexpr int kSeg = 32;
    float theta1 = g.start;
    for (size_t i = 0; i < data_.values.size(); ++i) {
        float theta2 = theta1 + g.dir * g.frac(data_, i);
        float sa0 = 2.0f * PI * theta1;
        float sa1 = 2.0f * PI * theta2;
        Color col = i < data_.colors.size() ? data_.colors[i]
                                            : ColorCycle::at(i);
        float thetam = PI * (theta1 + theta2);
        float expl = g.explodeAt(data_, i);
        Point2D wc{data_.center.x + expl * std::cos(thetam),
                   data_.center.y + expl * std::sin(thetam)};
        // Annular-sector polygon in pie data units → px.
        std::vector<Point2D> wedge;
        if (data_.innerRadius <= 0.0f) wedge.push_back(g.toPx(wc));
        for (int s = 0; s <= kSeg; ++s) {
            float a = sa0 + (sa1 - sa0) * float(s) / kSeg;
            wedge.push_back(g.toPx({wc.x + data_.radius * std::cos(a),
                                    wc.y + data_.radius * std::sin(a)}));
        }
        if (data_.innerRadius > 0.0f) {
            float ri = data_.radius * data_.innerRadius;
            for (int s = kSeg; s >= 0; --s) {
                float a = sa0 + (sa1 - sa0) * float(s) / kSeg;
                wedge.push_back(g.toPx({wc.x + ri * std::cos(a),
                                        wc.y + ri * std::sin(a)}));
            }
        }
        render::VectorCanvas::Pen edge;
        edge.color = Color::white();
        edge.width = 1.0f;
        c.polygon(wedge, col, &edge);
        theta1 = theta2;
    }

    const bool labelsOn = !std::isnan(data_.labelDistance);
    const bool pctOn = data_.autopctFn || !data_.autopct.empty();
    if (!labelsOn && !pctOn) return;
    constexpr float kFont = 14.0f;
    theta1 = g.start;
    for (size_t i = 0; i < data_.values.size(); ++i) {
        float frac = g.frac(data_, i);
        float theta2 = theta1 + g.dir * frac;
        float thetam = PI * (theta1 + theta2);
        float expl = g.explodeAt(data_, i);
        Point2D wc{data_.center.x + expl * std::cos(thetam),
                   data_.center.y + expl * std::sin(thetam)};
        if (labelsOn && i < data_.labels.size() &&
            !data_.labels[i].empty()) {
            float xt = wc.x + data_.labelDistance * data_.radius *
                           std::cos(thetam);
            float yt = wc.y + data_.labelDistance * data_.radius *
                           std::sin(thetam);
            Point2D lp = g.toPx({xt, yt});
            // VectorCanvas::text rot is clockwise-positive in Y-down
            // px — mpl's rad2deg(thetam) is CCW-positive → negate.
            float rot = data_.rotatelabels
                            ? -thetam + (xt > 0 ? 0.0f : PI) : 0.0f;
            c.text(lp, data_.labels[i], kFont, Color::black(), rot);
        }
        if (pctOn) {
            float xt = wc.x + data_.pctDistance * data_.radius *
                           std::cos(thetam);
            float yt = wc.y + data_.pctDistance * data_.radius *
                           std::sin(thetam);
            c.text(g.toPx({xt, yt}), autopctLabel(data_, frac), kFont,
                   Color::black());
        }
        theta1 = theta2;
    }
}
} // namespace volcano::plot
