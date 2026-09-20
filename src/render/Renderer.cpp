// volcano/render/Renderer.cpp
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorRenderer.hpp"
#include "volcano/render/VectorWriters.hpp"
#include <volcano/encode/MovieWriter.hpp>
#include <volcano/plot/Animation.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Interaction.hpp>
#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Widgets.hpp>
#include <volcano/text/MathText.hpp>
#include "TickLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <format>
#include <string>

namespace volcano::render {

namespace {

/// WidgetPainter adapter backed by the SpineRenderer + TextRenderer.
class Painter : public plot::WidgetPainter {
public:
    Painter(vk::CommandBuffer cmd, Renderer& r, vk::Rect2D scissor)
        : cmd_(cmd), r_(r), scissor_(scissor) {}

    void fillRect(plot::Rect2D rect, plot::Color c) override {
        r_.spineRenderer().drawFilledRect(cmd_, scissor_,
                                          r_.backend().extent(), rect, c);
    }
    void outlineRect(plot::Rect2D rect, plot::Color c, float w) override {
        r_.spineRenderer().drawRect(cmd_, scissor_,
                                    r_.backend().extent(), rect, c, w);
    }
    void line(std::span<const plot::Point2D> pts, plot::Color c,
              float w) override {
        r_.spineRenderer().drawLineStrip(cmd_, scissor_,
                                         r_.backend().extent(), pts, c, w);
    }
    void text(std::string_view s, float x, float y, plot::Color c,
              float scale) override {
        if (r_.textReady())
            r_.textRenderer().draw(cmd_, scissor_, s, x, y, c, scale);
    }
    Metrics measure(std::string_view s, float scale) override {
        if (!r_.textReady()) return {0.0f, 0.0f, 0.0f};
        auto m = r_.textRenderer().measureText(s, scale);
        return {m.width, m.height, m.ascent};
    }

private:
    vk::CommandBuffer cmd_;
    Renderer& r_;
    vk::Rect2D scissor_;
};

/// backend::InputEvent → plot::Event.
plot::Event translateEvent(const backend::InputEvent& ie) {
    plot::Event e{};
    switch (ie.type) {
        case backend::InputEvent::Type::ButtonPress:
            e.type = plot::Event::Type::ButtonPress; break;
        case backend::InputEvent::Type::ButtonRelease:
            e.type = plot::Event::Type::ButtonRelease; break;
        case backend::InputEvent::Type::Motion:
            e.type = plot::Event::Type::MotionNotify; break;
        case backend::InputEvent::Type::Scroll:
            e.type = plot::Event::Type::Scroll; break;
        case backend::InputEvent::Type::KeyPress:
            e.type = plot::Event::Type::KeyPress; break;
        case backend::InputEvent::Type::KeyRelease:
            e.type = plot::Event::Type::KeyRelease; break;
        case backend::InputEvent::Type::TextInput:
            e.type = plot::Event::Type::KeyPress; break;
        case backend::InputEvent::Type::Resize:
            e.type = plot::Event::Type::Resize; break;
        default:
            e.type = plot::Event::Type::MotionNotify; break;
    }
    e.x = ie.x; e.y = ie.y;
    e.button = ie.button;
    e.buttons = ie.buttons;
    e.dblclick = ie.dblclick;
    e.step = ie.step;
    if (ie.key != 0) e.key = std::string(1, ie.key);
    else if (!ie.text.empty()) e.key = ie.text;
    e.shift = ie.shift; e.ctrl = ie.ctrl; e.alt = ie.alt;
    e.width = ie.width; e.height = ie.height;
    return e;
}

} // namespace

bool Renderer::processInput(plot::Figure& figure) {
    for (const auto& ie : backend_.takeEvents()) {
        if (ie.type == backend::InputEvent::Type::Quit) return false;
        figure.dispatch(translateEvent(ie));
    }
    return true;
}

Renderer::Renderer(backend::IBackend& backend) : backend_(backend) {
    auto& ctx = backend_.context();
    pipelineCache_ = std::make_unique<core::PipelineCache>(ctx.device.handle());
    std::vector<vk::DescriptorPoolSize> sizes = {
        { vk::DescriptorType::eUniformBuffer, 256 },
        { vk::DescriptorType::eStorageBuffer, 256 },
        { vk::DescriptorType::eCombinedImageSampler, 64 },
    };
    descriptorPool_ = std::make_unique<core::DescriptorPool>(
        ctx.device.handle(), sizes, 512,
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
}

Renderer::~Renderer() = default;

void Renderer::prepare(plot::Figure& figure) {
    auto& ctx = backend_.context();
    // Init text renderer once.
    if (!textInited_) {
        textRenderer_.init(ctx.device.handle(), ctx.allocator.handle(),
                           backend_.renderPass(), backend_.sampleCount(),
                           *pipelineCache_, *descriptorPool_);
        textInited_ = true;
        // Pre-render ASCII glyphs and upload atlas texture.
        textRenderer_.prepareAtlas(ctx.device.graphicsQueue(),
                                   ctx.graphicsPool.handle());
        textReady_ = true;
    }
    // Init spine renderer once.
    if (!spineInited_) {
        spineRenderer_.init(ctx.device.handle(), ctx.allocator.handle(),
                            backend_.renderPass(), backend_.sampleCount(),
                            *pipelineCache_, *descriptorPool_);
        spineInited_ = true;
    }
    // Init GPU autoscale reduce pipeline once.
    if (!reduceInited_) {
        reduceRenderer_.init(ctx.device.handle(), ctx.allocator.handle(),
                             ctx.device.computeQueue(),
                             ctx.computePool.handle());
        reduceInited_ = true;
    }

    // Upload all plot GPU resources first, so the GPU autoscale reduce can
    // operate on the uploaded point buffers.
    for (auto& p : figure.placements()) {
        for (auto& plot : p.axes->plots()) {
            plot->prepare(*this);
        }
    }
    // Compute viewports via GPU parallel min/max reduce (per-layer CPU
    // fallback for plot types without GPU buffers).
    for (auto& p : figure.placements()) {
        p.axes->autoscaleGpu(reduceRenderer_);
    }
    prepared_ = true;
}

text::TextRenderer::TextMetrics
Renderer::measureRichText(std::string_view text, float scale) {
    if (!text::containsMath(text))
        return textRenderer_.measureText(text, scale);
    auto lay = text::layoutMathText(text, scale,
        [this](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc);
            return text::TextMeasure{m.width, m.height, m.ascent};
        });
    return {lay.width, lay.ascent + lay.descent, lay.ascent};
}

void Renderer::drawRichText(vk::CommandBuffer cmd, vk::Rect2D scissor,
                            std::string_view text, float x, float y,
                            plot::Color color, float scale, float rotation,
                            plot::HAlign lineAlign) {
    if (!textReady_ || text.empty()) return;
    if (!text::containsMath(text)) {
        textRenderer_.draw(cmd, scissor, text, x, y, color, scale,
                           rotation, lineAlign);
        return;
    }
    // Layout the math segments and emit each positioned run/rule.
    auto lay = text::layoutMathText(text, scale,
        [this](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc);
            return text::TextMeasure{m.width, m.height, m.ascent};
        });
    float cosR = std::cos(rotation), sinR = std::sin(rotation);
    auto rot = [&](float px, float py) {
        return plot::Point2D{x + px * cosR - py * sinR,
                             y + px * sinR + py * cosR};
    };
    for (const auto& r : lay.runs) {
        auto p = rot(r.x, r.baseline);
        textRenderer_.draw(cmd, scissor, r.text, p.x, p.y,
                           color, scale * r.scale, rotation);
    }
    for (const auto& rl : lay.rules) {
        auto p0 = rot(rl.x0, rl.y0);
        auto p1 = rot(rl.x1, rl.y0);
        plot::Point2D pts[2] = {p0, p1};
        spineRenderer_.drawLineStrip(cmd, scissor, backend_.extent(),
                                     std::span{pts, 2}, color, rl.thickness);
    }
}

void Renderer::drawText(vk::CommandBuffer cmd, const plot::Axes& axes,
                        plot::Rect2D rect) {
    if (!textReady_) return;

    const auto& style = axes.style();
    // Skip text rendering if axes are not visible (e.g. flat test style).
    if (!style.xAxis.visible && !style.yAxis.visible &&
        style.title.text.empty()) {
        return;
    }
    // Text scale: style.fontSize is in points at style.dpi; the atlas
    // renders 16px at scale 1.
    float scale = style.fontSize * style.dpi / (72.0f * 16.0f);
    auto labelColor = style.xAxis.color;

    // Use the full framebuffer as the scissor rect so text outside the
    // axes rect (labels, title) is not clipped.
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Gap between a tick mark and its label comes from the axis's tick
    // pad (matplotlib xtick.major.pad=3.5pt / xtick.minor.pad=3.4pt).

    // --- Title ---
    if (!style.title.text.empty()) {
        auto m = measureRichText(style.title.text, scale);
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        // Position above the axes: text bottom at rect.y - padding.
        // text bottom = y + descent = y + (height - ascent)
        // So y = rect.y - pad - (height - ascent) = rect.y - pad - height + ascent
        float cy = float(rect.y) - style.title.pad * kPtToPx -
                   m.height + m.ascent;
        drawRichText(cmd, fullRect,
            style.title.text, cx, cy, style.title.color, scale,
            style.title.font.rotation, plot::HAlign::Center);
        // Faux bold: second pass offset ~0.6px (no bold face in the atlas).
        if (style.title.weight == "bold" ||
            style.title.font.weight == "bold")
            drawRichText(cmd, fullRect,
                style.title.text, cx + 0.6f, cy, style.title.color, scale,
                style.title.font.rotation, plot::HAlign::Center);
    }

    // Polar axes: theta/r labels are drawn with the polar spine
    // furniture; skip rectilinear tick/axis labels.
    if (axes.projection().kind == plot::ProjectionKind::Polar) return;

    // --- Tick labels ---
    // Positioning (matching matplotlib):
    //   X labels: horizontal center at tick x, top border at tick mark bottom + spacing.
    //   Y labels: vertical center at tick y, right border at tick mark left + spacing.
    // The text renderer's draw(x, y) uses (x, y) as the baseline origin.
    //   text top    = y - ascent
    //   text bottom = y - ascent + height = y + descent
    //   text left   = x
    //   text right  = x + width
    //   vertical center = y - ascent + height/2
    const auto& vp = axes.viewport();
    // 3D plots (projection="3d") draw no 2D tick/axis labels — mplot3d
    // renders its own pane labels. Title still draws.
    bool has3D = std::ranges::any_of(axes.drawOrder(),
        [](const plot::IPlot* pl) { return pl->is3D(); });
    if (!has3D && style.xAxis.visible) {
        const auto& tc = style.xAxis.ticks;
        auto xTicks = axisTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                float(rect.width), style.fontSize,
                                style.dpi, false);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, xTicks, style, axes.xscale(),
                                             vp.x.min, vp.x.max,
                                             defaultFmt, strFmt, logFmt);
        bool top = axes.xTicksTop();
        float edge = top ? float(rect.y) : float(rect.y + rect.height);
        float d = top ? -1.0f : 1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        const float kTickSpacing = tc.majorPad * kPtToPx;
        float tickLabelH = 0.0f;
        int i = 0;
        for (float tick : xTicks) {
            float px = rect.x + axes.dataToFraction({tick, 0.0f}).x * rect.width;
            if (px < rect.x || px > rect.x + rect.width) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, scale);
            float rot = style.xAxis.tickFont.rotation;
            tickLabelH = std::max(tickLabelH,
                m.ascent + m.width * std::abs(std::sin(rot)));
            // bottom: text top at tickEnd + spacing (y - ascent = top)
            // top: text bottom at tickEnd - spacing (y+descent = bottom)
            float baseY = top ? tickEnd - kTickSpacing - m.height + m.ascent
                              : tickEnd + kTickSpacing + m.ascent;
            float x, y = baseY;
            if (rot != 0.0f) {
                // Rotated: anchor the baseline's right end at the tick
                // (matplotlib's ha='right' for rotated xticklabels).
                float cosR = std::cos(rot), sinR = std::sin(rot);
                x = px - m.width * cosR;
                y = baseY - m.width * sinR;
            } else {
                // Horizontal center at px; label sits outside the tick mark.
                x = px - m.width * 0.5f;
            }
            drawRichText(cmd, fullRect, label, x, y, labelColor, scale, rot);
        }
        // Offset/scientific text at the axis end (matplotlib "+1e4").
        auto off = fmt->offsetText();
        if (!off.empty()) {
            auto m = measureRichText(off, scale * 0.8f);
            float x = rect.x + rect.width - m.width * 0.5f;
            float y = top ? rect.y - outLen - kTickSpacing - m.height + m.ascent
                          : tickEnd + kTickSpacing + m.height + m.ascent * 0.2f;
            drawRichText(cmd, fullRect, off, x, y, labelColor, scale * 0.8f);
        }
        // Minor tick labels: an explicit minor formatter, or mpl's
        // default log-axis minor labels (LogFormatterSciNotation with
        // minor_thresholds suppression on crowded axes).
        if (logMinorLabels(tc, axes.xscale())) {
            auto minor = axisMinorTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                        xTicks);
            plot::LogFormatterSciNotation defMinorFmt;
            plot::Formatter* mfmt = tc.minorFormatter
                                        ? tc.minorFormatter.get()
                                        : &defMinorFmt;
            mfmt->setViewInterval(vp.x.min, vp.x.max);
            mfmt->setLocs(minor);
            float mScale = scale;
            float mEnd = edge + d * tc.minorSize * kPtToPx *
                            (1.0f - tickInFrac(tc));
            float mGap = tc.minorPad * kPtToPx;
            int mi = 0;
            for (float t : minor) {
                float px = rect.x + axes.dataToFraction({t, 0.0f}).x * rect.width;
                if (px < rect.x || px > rect.x + rect.width) { ++mi; continue; }
                auto label = mfmt->format(t, mi++);
                if (label.empty()) continue;
                auto m = measureRichText(label, mScale);
                float x = px - m.width * 0.5f;
                float y = mEnd + mGap + m.ascent;
                drawRichText(cmd, fullRect, label, x, y, labelColor, mScale);
            }
        }

        // --- Secondary x axis (mpl secondary_xaxis): top-side tick
        // labels in transformed units via the inverse map.
        if (auto sec = axes.secondaryX()) {
            float slo = sec->forward(vp.x.min), shi = sec->forward(vp.x.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.width), style.fontSize,
                                    style.dpi, false);
            plot::ScalarFormatter sFmt;
            sFmt.setLocs(sTicks);
            for (float st : sTicks) {
                float v = sec->inverse(st);
                float px = rect.x + axes.dataToFraction({v, 0.0f}).x *
                           rect.width;
                if (px < rect.x || px > rect.x + rect.width) continue;
                auto label = sFmt.format(st, 0);
                if (label.empty()) continue;
                auto m = measureRichText(label, scale);
                drawRichText(cmd, fullRect, label, px - m.width * 0.5f,
                             rect.y - outLen - kTickSpacing - m.height +
                                 m.ascent, labelColor, scale);
            }
        }

        // --- X axis label --- centered below the tick labels (or above
        // when ticks are on top), using the measured tick-label depth.
        if (!style.xAxis.label.empty()) {
            auto m = measureRichText(style.xAxis.label, scale);
            float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
            float cy = edge + d * (outLen + kTickSpacing + tickLabelH +
                                   style.xAxis.labelPad * kPtToPx) +
                       m.ascent - (top ? m.height : 0.0f);
            drawRichText(cmd, fullRect,
                style.xAxis.label, cx, cy, style.xAxis.labelColor, scale,
                style.xAxis.labelFont.rotation, plot::HAlign::Center);
        }
    }

    if (!has3D && style.yAxis.visible) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                float(rect.height), style.fontSize,
                                style.dpi, true);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, yTicks, style, axes.yscale(),
                                             vp.y.min, vp.y.max,
                                             defaultFmt, strFmt, logFmt);
        bool right = axes.yTicksRight();
        float edge = right ? float(rect.x + rect.width) : float(rect.x);
        float d = right ? 1.0f : -1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        const float kTickSpacing = tc.majorPad * kPtToPx;
        float tickLabelW = 0.0f;
        int i = 0;
        for (float tick : yTicks) {
            float py = rect.y + rect.height -
                       axes.dataToFraction({0.0f, tick}).y * rect.height;
            if (py < rect.y || py > rect.y + rect.height) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, scale);
            float rot = style.yAxis.tickFont.rotation;
            tickLabelW = std::max(tickLabelW,
                m.width * std::abs(std::cos(rot)) +
                m.height * std::abs(std::sin(rot)));
            // Label sits outside the tick mark, vertically centered at py.
            float baseY = py + m.ascent - m.height * 0.5f;
            float x, y = baseY;
            if (rot != 0.0f) {
                // Rotated: anchor the baseline's edge at the tick mark,
                // keeping the same vertical center (mpl va='center').
                float cosR = std::cos(rot), sinR = std::sin(rot);
                float edge = right ? tickEnd + kTickSpacing
                                   : tickEnd - kTickSpacing;
                x = right ? edge : edge - m.width * cosR;
                y = right ? baseY : baseY - m.width * sinR;
            } else {
                x = right ? tickEnd + kTickSpacing
                          : tickEnd - kTickSpacing - m.width;
            }
            drawRichText(cmd, fullRect, label, x, y, labelColor, scale, rot);
        }
        // Offset text above the top of the y axis.
        auto off = fmt->offsetText();
        if (!off.empty()) {
            auto m = measureRichText(off, scale * 0.8f);
            float x = right ? rect.x + rect.width - m.width : rect.x;
            float y = rect.y - kTickSpacing;
            drawRichText(cmd, fullRect, off, x, y, labelColor, scale * 0.8f);
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
            float mScale = scale;
            float mEnd = edge + d * tc.minorSize * kPtToPx *
                            (1.0f - tickInFrac(tc));
            float mGap = tc.minorPad * kPtToPx;
            int mi = 0;
            for (float t : minor) {
                float py = rect.y + rect.height -
                           axes.dataToFraction({0.0f, t}).y * rect.height;
                if (py < rect.y || py > rect.y + rect.height) { ++mi; continue; }
                auto label = mfmt->format(t, mi++);
                if (label.empty()) continue;
                auto m = measureRichText(label, mScale);
                float x = right ? mEnd + mGap
                                : mEnd - mGap - m.width;
                float y = py + m.ascent - m.height * 0.5f;
                drawRichText(cmd, fullRect, label, x, y, labelColor, mScale);
            }
        }

        // --- Y axis label --- Rotate -90° (clockwise in screen space,
        // Y-down) so the label reads bottom-to-top. The rotation origin is
        // the text baseline (x, y). After rotation:
        //   - text width becomes vertical extent (upward from origin)
        //   - ascent becomes leftward extent, descent becomes rightward
        // We want: vertical center at axes middle, positioned outside the
        // tick labels using their measured width.
        if (!style.yAxis.label.empty()) {
            auto m = measureRichText(style.yAxis.label, scale);
            // Y position: y - width/2 = axes vertical center
            float oy = rect.y + rect.height / 2.0f + m.width / 2.0f;
            // Center of rotated text = x + m.height/2 - m.ascent, so
            // x = centerPos - m.height/2 + m.ascent.
            float centerPos = edge + d * (outLen + kTickSpacing +
                                          tickLabelW +
                                          style.yAxis.labelPad * kPtToPx +
                                          m.height * 0.5f);
            float ox = centerPos - m.height / 2.0f + m.ascent;
            constexpr float kRotMinus90 = -1.5707963267948966f; // -π/2
            drawRichText(cmd, fullRect,
                style.yAxis.label, ox, oy, style.yAxis.labelColor, scale,
                kRotMinus90 + style.yAxis.labelFont.rotation,
                plot::HAlign::Center);
        }

        // --- Secondary y axis (matplotlib secondary_yaxis): right-side
        // tick labels in transformed units positioned via the inverse map.
        if (auto sec = axes.secondaryY()) {
            float slo = sec->forward(vp.y.min), shi = sec->forward(vp.y.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.height), style.fontSize,
                                    style.dpi, true);
            plot::ScalarFormatter sFmt;
            sFmt.setLocs(sTicks);
            float rEdge = float(rect.x + rect.width);
            for (float st : sTicks) {
                float v = sec->inverse(st);
                float py = rect.y + rect.height -
                           axes.dataToFraction({0.0f, v}).y * rect.height;
                if (py < rect.y || py > rect.y + rect.height) continue;
                auto label = sFmt.format(st, 0);
                if (label.empty()) continue;
                auto m = measureRichText(label, scale);
                drawRichText(cmd, fullRect, label,
                             rEdge + outLen + kTickSpacing,
                             py + m.ascent - m.height * 0.5f,
                             labelColor, scale);
            }
        }
    }
}

void Renderer::drawSpines(vk::CommandBuffer cmd, const plot::Axes& axes,
                           plot::Rect2D rect) {
    if (!spineInited_) return;
    const auto& style = axes.style();
    if (!style.xAxis.visible && !style.yAxis.visible) return;

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Draw the border as pixel-aligned filled quads (matplotlib draws ~1px
    // spines). Filled quads aligned to integer pixel coordinates get 100%
    // coverage → pure black even under MSAA, unlike a stroked line centered
    // on the boundary which spreads across two pixels.
    auto spineColor = style.xAxis.color;
    float t = std::max(style.xAxis.lineWidth, 1.0f);
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    const auto& sp = axes.spines();
    auto quad = [&](float qx0, float qy0, float qx1, float qy1) {
        spineRenderer_.drawFilledRect(cmd, fullRect, ext,
            plot::Rect2D{qx0, qy0, qx1 - qx0, qy1 - qy0}, spineColor);
    };
    if (sp.bottom) quad(x0, y1 - t, x1, y1);
    if (sp.top)    quad(x0, y0, x1, y0 + t);
    if (sp.left)   quad(x0, y0, x0 + t, y1);
    if (sp.right)  quad(x1 - t, y0, x1, y1);

    // Draw tick marks. Use >=2.0px width for the same MSAA reason.
    // Tick positions are converted to axes fractions so scale-aware
    // placement matches the labels.
    const auto& vp = axes.viewport();
    auto toFrac = [&](const std::vector<float>& ticks, bool yAxis) {
        std::vector<float> fr;
        fr.reserve(ticks.size());
        for (float t : ticks)
            fr.push_back(yAxis ? axes.dataToFraction({0.0f, t}).y
                               : axes.dataToFraction({t, 0.0f}).x);
        return fr;
    };
    auto drawAxisTicks = [&](const plot::AxisStyle& as,
                             const plot::AxisScale& scale,
                             float lo, float hi, bool yAxis, bool farSide) {
        const auto& tc = as.ticks;
        float inF = tickInFrac(tc);
        float axisLen = yAxis ? float(rect.height) : float(rect.width);
        auto majors = axisTicks(tc, scale, lo, hi, axisLen,
                                style.fontSize, style.dpi, yAxis);
        auto majorFrac = toFrac(majors, yAxis);
        spineRenderer_.drawTicks(cmd, fullRect, ext, rect, majorFrac,
                                 as.color, tc.majorSize * kPtToPx,
                                 yAxis, 0.0f, 1.0f, inF, farSide,
                                 std::max(tc.majorWidth * kPtToPx, 1.5f));
        auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
        if (!minors.empty()) {
            auto minorFrac = toFrac(minors, yAxis);
            spineRenderer_.drawTicks(cmd, fullRect, ext, rect, minorFrac,
                                     as.color, tc.minorSize * kPtToPx,
                                     yAxis, 0.0f, 1.0f, inF, farSide,
                                     std::max(tc.minorWidth * kPtToPx, 1.0f));
        }
    };
    if (style.xAxis.visible) {
        // Marks on the top: either moved (xTicksTop) or in addition to
        // the bottom (xTickMarksTop, mpl tick_params(top=True)).
        if (axes.xTicksTop() || axes.xTickMarksTop())
            drawAxisTicks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                          false, true);
        if (!axes.xTicksTop() || axes.xTickMarksTop())
            drawAxisTicks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                          false, false);
    }
    if (style.yAxis.visible) {
        if (axes.yTicksRight() || axes.yTickMarksRight())
            drawAxisTicks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                          true, true);
        if (!axes.yTicksRight() || axes.yTickMarksRight())
            drawAxisTicks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                          true, false);
    }
}

/// Draw tick-aligned grid lines for one axes. xAxis.grid draws vertical
/// lines at x tick positions, yAxis.grid draws horizontal lines at y
/// ticks. gridWhich selects "major"/"minor"/"both"; minor lines use the
/// minorGrid* styling. Grid line style "-"/"--"/":"/"-." maps to the
/// usual dash patterns. Lines are clipped to the axes rect.
void Renderer::drawGrid(vk::CommandBuffer cmd, const plot::Axes& axes,
                        plot::Rect2D rect) {
    if (!spineInited_) return;
    const auto& style = axes.style();
    const auto& vp = axes.viewport();
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};

    const float x0 = float(rect.x), y0 = float(rect.y);
    const float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);

    auto drawSet = [&](bool yAxis, std::span<const float> fracs,
                       plot::Color color, float widthPt,
                       const std::string& lineStyle) {
        if (fracs.empty()) return;
        const float w = std::max(widthPt * kPtToPx, 1.0f);
        plot::StrokeParams sp;
        sp.width = w;
        if (auto ls = plot::lineStyleFromString(lineStyle);
            ls && *ls != plot::LineStyle::Solid)
            sp.dashes = plot::dashPattern(*ls, w);
        // Stroke every grid line into one mesh: a single drawTriangles
        // call keeps scratch-buffer pressure low even with fine dashes.
        std::vector<plot::Point2D> tris;
        for (float f : fracs) {
            if (f < 0.0f || f > 1.0f) continue;
            plot::Point2D pts[2];
            if (yAxis) {
                float py = y0 + (1.0f - f) * float(rect.height);
                pts[0] = {x0, py};
                pts[1] = {x1, py};
            } else {
                float px = x0 + f * float(rect.width);
                pts[0] = {px, y0};
                pts[1] = {px, y1};
            }
            auto mesh = plot::strokePolyline(pts, sp);
            tris.insert(tris.end(), mesh.verts.begin(), mesh.verts.end());
        }
        if (!tris.empty())
            spineRenderer_.drawTriangles(cmd, clip, backend_.extent(),
                                         tris, color);
    };

    auto drawAxis = [&](const plot::AxisStyle& as,
                        const plot::AxisScale& scale,
                        float lo, float hi, bool yAxis) {
        if (!as.grid) return;
        const auto& tc = as.ticks;
        float axisLen = yAxis ? float(rect.height) : float(rect.width);
        auto majors = axisTicks(tc, scale, lo, hi, axisLen,
                                style.fontSize, style.dpi, yAxis);
        auto toFrac = [&](std::span<const float> ticks) {
            std::vector<float> fr;
            fr.reserve(ticks.size());
            for (float t : ticks)
                fr.push_back(yAxis ? axes.dataToFraction({0.0f, t}).y
                                   : axes.dataToFraction({t, 0.0f}).x);
            return fr;
        };
        if (as.gridWhich != "minor")
            drawSet(yAxis, toFrac(majors), as.gridColor, as.gridLineWidth,
                    as.gridLineStyle);
        if (as.gridWhich == "minor" || as.gridWhich == "both") {
            auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
            drawSet(yAxis, toFrac(minors), as.minorGridColor,
                    as.minorGridLineWidth, as.minorGridLineStyle);
        }
    };

    drawAxis(style.xAxis, axes.xscale(), vp.x.min, vp.x.max, false);
    drawAxis(style.yAxis, axes.yscale(), vp.y.min, vp.y.max, true);
}

// ── Polar axes furniture ────────────────────────────────────────────────────
// matplotlib projection="polar": the axes frame is a circle at rmax, the
// grid is radial spokes at theta ticks plus concentric circles at r ticks,
// theta tick labels sit just outside the frame (0°…315°, mpl's default
// ThetaLocator base π/4), and r labels run along the 22.5° radial
// (mpl rlabel_position default).

namespace {
/// Map polar data (theta, r) to canvas pixels through the axes' own
/// transform (projection + viewport + rect).
inline plot::Point2D polarToPx(const plot::Axes& axes, plot::Rect2D rect,
                               float theta, float r) {
    auto f = axes.dataToFraction({theta, r});
    return {float(rect.x) + f.x * float(rect.width),
            float(rect.y) + (1.0f - f.y) * float(rect.height)};
}

/// Polyline approximation of the r-circle (128 segments).
std::vector<plot::Point2D> polarCircle(const plot::Axes& axes,
                                       plot::Rect2D rect, float r,
                                       int n = 128) {
    std::vector<plot::Point2D> pts;
    pts.reserve(n + 1);
    constexpr float kTwoPi = 6.2831853071795865f;
    for (int i = 0; i <= n; ++i)
        pts.push_back(polarToPx(axes, rect, kTwoPi * float(i) / float(n), r));
    return pts;
}

/// Stroke a pixel-space polyline into a triangle mesh and draw it.
void strokePxPoly(vk::CommandBuffer cmd,
                  primitives::SpineRenderer& sr,
                  vk::Rect2D clip, vk::Extent2D ext,
                  std::span<const plot::Point2D> pts,
                  plot::Color color, float widthPx) {
    plot::StrokeParams sp;
    sp.width = std::max(widthPx, 1.0f);
    auto mesh = plot::strokePolyline(pts, sp);
    if (!mesh.verts.empty())
        sr.drawTriangles(cmd, clip, ext, mesh.verts, color);
}
} // namespace

void Renderer::drawPolarGrid(vk::CommandBuffer cmd, const plot::Axes& axes,
                             plot::Rect2D rect) {
    const auto& style = axes.style();
    const auto& vp = axes.viewport();
    float rmax = std::max(std::fabs(vp.y.min), std::fabs(vp.y.max));
    if (rmax <= 0.0f) rmax = 1.0f;
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    auto ext = backend_.extent();
    constexpr float kPi = 3.14159265358979323846f;

    // Radial spokes at theta ticks (x-axis grid): mpl ThetaLocator → π/4,
    // or explicit set_thetagrids positions (degrees).
    if (style.xAxis.grid) {
        std::vector<float> thetas;
        if (!axes.thetagrids().empty())
            for (float d : axes.thetagrids())
                thetas.push_back(d * kPi / 180.0f);
        else
            for (int k = 0; k < 8; ++k) thetas.push_back(float(k) * kPi / 4.0f);
        auto c = polarToPx(axes, rect, 0.0f, 0.0f);
        for (float th : thetas) {
            plot::Point2D pts[2] = {c, polarToPx(axes, rect, th, rmax)};
            strokePxPoly(cmd, spineRenderer_, clip, ext, pts,
                         style.xAxis.gridColor,
                         style.xAxis.gridLineWidth * kPtToPx);
        }
    }
    // Concentric circles at r ticks (y-axis grid), or set_rgrids radii.
    if (style.yAxis.grid) {
        auto rTicks = !axes.rgrids().empty()
            ? axes.rgrids()
            : axisTicks(style.yAxis.ticks, axes.yscale(),
                        vp.y.min, vp.y.max, float(rect.height),
                        style.fontSize, style.dpi, true);
        for (float r : rTicks) {
            if (r <= 0.0f || r > rmax) continue;
            auto circ = polarCircle(axes, rect, r);
            strokePxPoly(cmd, spineRenderer_, clip, ext, circ,
                         style.yAxis.gridColor,
                         style.yAxis.gridLineWidth * kPtToPx);
        }
    }
}

void Renderer::drawPolarSpineAndLabels(vk::CommandBuffer cmd,
                                       const plot::Axes& axes,
                                       plot::Rect2D rect) {
    const auto& style = axes.style();
    const auto& vp = axes.viewport();
    float rmax = std::max(std::fabs(vp.y.min), std::fabs(vp.y.max));
    if (rmax <= 0.0f) rmax = 1.0f;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    constexpr float kPi = 3.14159265358979323846f;

    // Circular outer spine at rmax.
    auto circ = polarCircle(axes, rect, rmax);
    strokePxPoly(cmd, spineRenderer_, fullRect, ext, circ,
                 style.xAxis.color,
                 std::max(style.xAxis.lineWidth * kPtToPx, 1.0f));

    if (!textReady_) return;
    float scale = style.fontSize * style.dpi / (72.0f * 16.0f);
    auto labelColor = style.xAxis.color;
    const auto& tc = style.xAxis.ticks;
    float pad = tc.majorPad * kPtToPx + tc.majorSize * kPtToPx;

    // Theta labels at each π/4 (or set_thetagrids degrees), centered on
    // the frame normal.
    std::vector<float> thetaDegs;
    if (!axes.thetagrids().empty()) thetaDegs = axes.thetagrids();
    else for (int k = 0; k < 8; ++k) thetaDegs.push_back(float(k * 45));
    for (float deg : thetaDegs) {
        float th = deg * kPi / 180.0f;
        auto label = std::format("{}°", int(std::lround(deg)));
        auto m = measureRichText(label, scale);
        auto c = polarToPx(axes, rect, th, rmax);
        auto edge = polarToPx(axes, rect, th + 0.01f, rmax);
        // Outward direction ≈ direction of increasing r at this theta.
        auto c0 = polarToPx(axes, rect, th, rmax * 0.9f);
        float dx = c.x - c0.x, dy = c.y - c0.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) { dx = edge.x - c.x; dy = edge.y - c.y;
            len = std::max(std::sqrt(dx * dx + dy * dy), 1.0f); }
        dx /= len; dy /= len;
        float lx = c.x + dx * (pad + m.height * 0.5f);
        float ly = c.y + dy * (pad + m.height * 0.5f);
        drawRichText(cmd, fullRect, label, lx - m.width * 0.5f,
                     ly + m.ascent - m.height * 0.5f,
                     labelColor, scale, 0.0f, plot::HAlign::Center);
    }

    // r labels along the 22.5° radial (mpl rlabel_position).
    if (style.yAxis.visible) {
        const auto& ytc = style.yAxis.ticks;
        auto rTicks = !axes.rgrids().empty()
            ? axes.rgrids()
            : axisTicks(ytc, axes.yscale(), vp.y.min, vp.y.max,
                        float(rect.height), style.fontSize,
                        style.dpi, true);
        plot::ScalarFormatter fmt;
        fmt.setLocs(rTicks);
        float labelAng = 22.5f * kPi / 180.0f;
        int i = 0;
        for (float r : rTicks) {
            if (r <= 0.0f || r > rmax) { ++i; continue; }
            auto label = fmt.format(r, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, scale * 0.8f);
            auto p = polarToPx(axes, rect, labelAng, r);
            drawRichText(cmd, fullRect, label, p.x - m.width * 0.5f,
                         p.y + m.ascent - m.height * 0.5f,
                         labelColor, scale * 0.8f,
                         0.0f, plot::HAlign::Center);
        }
    }
}

void Renderer::drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                          plot::Rect2D rect) {
    if (!spineInited_ || !textReady_) return;
    const auto& style = axes.style();
    const auto& lg = style.legend;
    if (!lg.visible) { axes.setLegendBox({}); return; }

    // Collect legend entries (label + color + marker) from all plot
    // layers; a handler_map entry overrides the plot's own handle.
    struct LegendEntry { std::string label; plot::Color color; plot::LegendMarker marker; };
    std::vector<LegendEntry> entries;
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
    if (entries.empty()) { axes.setLegendBox({}); return; }

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Font scale: same points→pixels mapping as the rest of the text
    // pipeline (16px atlas reference at 72dpi).
    const float scale = lg.font.size * style.dpi / (72.0f * 16.0f);
    const float fontPx = 16.0f * scale;
    const float pad = lg.borderPad * fontPx + (lg.fancyBox ? 2.0f : 0.0f);
    const float handleW = lg.handleLength * fontPx;
    const float textGap = lg.handleTextPad * fontPx;
    const float colGap = lg.columnSpacing * fontPx;
    const float rowSep = lg.labelSpacing * fontPx;

    // mpl handle box: `height = handleheight*fontsize - descent` tall
    // above the text baseline, `descent` below it, where
    // descent = 0.35*fontsize*(handleheight - 0.7) (legend.py heuristic).
    const float hBelow = 0.35f * fontPx * (lg.handleHeight - 0.7f);
    const float hBoxH = lg.handleHeight * fontPx - hBelow;
    const float hAbove = hBoxH - hBelow;

    // Column-major split into columns (matplotlib: each column is a
    // contiguous run of `rows` entries).
    const int n = static_cast<int>(entries.size());
    const int rows = lg.nrows > 0 ? lg.nrows
                                  : (n + std::max(1, lg.ncols) - 1) /
                                        std::max(1, lg.ncols);
    const int cols = (n + rows - 1) / rows;

    // Measure each label once (mpl TextArea extents drive packing).
    struct ItemMetric { float w, ascent, above, below; };
    std::vector<ItemMetric> im(n);
    for (int i = 0; i < n; ++i) {
        auto m = measureRichText(entries[i].label, scale);
        im[i].w = m.width;
        im[i].ascent = m.ascent;
        im[i].above = std::max(m.ascent, hAbove);
        im[i].below = std::max(m.height - m.ascent, hBelow);
    }

    // Per-column width = handle + text gap + widest label in the column.
    // Per-column height = sum of item extents + labelspacing seps (each
    // column is its own VPacker in mpl — rows need not align).
    std::vector<float> colW(cols, 0.0f), colH(cols, 0.0f);
    for (int c = 0; c < cols; ++c) {
        float maxW = 0, h = 0;
        for (int r = 0; r < rows; ++r) {
            int idx = c * rows + r;
            if (idx >= n) break;
            maxW = std::max(maxW, im[idx].w);
            h += im[idx].above + im[idx].below + (r ? rowSep : 0.0f);
        }
        colW[c] = handleW + textGap + maxW;
        colH[c] = h;
    }
    float contentW = 0;
    for (float w : colW) contentW += w;
    contentW += colGap * (cols - 1);
    const float contentH = *std::ranges::max_element(colH);

    // Title row (mpl packs the title TextArea + labelspacing sep above
    // the handle box; the sep applies even when the title is empty).
    float titleW = 0.0f, titleH = 0.0f;
    const float titleScale = lg.titleFont.size * style.dpi / (72.0f * 16.0f);
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, titleScale);
        titleW = m.width;
        titleH = m.height;
    }

    const float boxW = pad * 2 + std::max(contentW, titleW);
    const float boxH = pad * 2 + titleH + rowSep + contentH;

    // Resolve loc → the axes-space anchor (fx,fy) and the box-fraction
    // point (bx,by) placed there. (0,0)=bottom-left, (1,1)=top-right.
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
        return {1, 1, 1, 1}; // "best"/"upper right"/"0"/"1"/unknown
    };
    LocAnchor la = parseLoc(lg.location);
    if (lg.location == "best" || lg.location == "0") {
        // matplotlib loc='best': evaluate the inside-corner candidates
        // and pick the one where the legend box overlaps the least data.
        // Candidate order matches mpl's search order.
        const LocAnchor cands[4] = {
            {1, 1, 1, 1},  // upper right
            {0, 1, 0, 1},  // upper left
            {0, 0, 0, 0},  // lower left
            {1, 0, 1, 0},  // lower right
        };
        const float fw = boxW / std::max(float(rect.width), 1.0f);
        const float fh = boxH / std::max(float(rect.height), 1.0f);
        float bestScore = std::numeric_limits<float>::infinity();
        for (const auto& c : cands) {
            float fx0 = c.fx > 0.5f ? 1.0f - fw : 0.0f;
            float fy0 = c.fy > 0.5f ? 1.0f - fh : 0.0f;
            auto d0 = axes.fractionToData({fx0, fy0});
            auto d1 = axes.fractionToData({fx0 + fw, fy0 + fh});
            plot::Range xr{std::min(d0.x, d1.x), std::max(d0.x, d1.x)};
            plot::Range yr{std::min(d0.y, d1.y), std::max(d0.y, d1.y)};
            float score = 0.0f;
            for (const auto& plot : axes.plots())
                score += plot->occupancy(xr, yr);
            if (score < bestScore) { bestScore = score; la = c; }
        }
    }

    // Anchor pixel position: bbox_to_anchor (anchorSpace coords) when set,
    // else the loc's axes fraction.
    const bool hasAnchor = lg.anchorX >= 0.0f || lg.anchorY >= 0.0f;
    float afx = hasAnchor ? lg.anchorX : la.fx;
    float afy = hasAnchor ? lg.anchorY : la.fy;
    float px, py;
    if (hasAnchor && lg.anchorSpace == plot::CoordSystem::Figure) {
        px = afx * ext.width;
        py = (1.0f - afy) * ext.height;
    } else {
        px = rect.x + afx * rect.width;
        py = rect.y + (1.0f - afy) * rect.height;
    }

    // Without an explicit anchor, inset the legend toward the axes
    // interior by borderaxespad (only for inside placements).
    if (!hasAnchor) {
        const float m = lg.borderAxesPad * fontPx;
        if (la.bx > 0.5f) px -= m;
        else if (la.bx < 0.5f) px += m;
        if (la.by > 0.5f) py += m;
        else if (la.by < 0.5f) py -= m;
    }

    // Drag offset (mpl draggable legend): pixel displacement applied
    // to the resolved anchor point.
    px += lg.dragOffset.x;
    py += lg.dragOffset.y;

    const float boxX = px - la.bx * boxW;
    const float boxY = py - (1.0f - la.by) * boxH;
    const plot::Rect2D boxRect{boxX, boxY, boxW, boxH};
    axes.setLegendBox(boxRect);

    // Drop shadow behind the box.
    if (lg.shadow) {
        const float so = fontPx * 0.25f;
        spineRenderer_.drawFilledRect(cmd, fullRect, ext,
            {boxX + so, boxY + so, boxW, boxH},
            plot::Color::fromRgba8(0, 0, 0, 100));
    }

    if (lg.frameOn) {
        // Semi-transparent background.
        auto bg = lg.faceColor;
        bg.a *= lg.frameAlpha;
        spineRenderer_.drawFilledRect(cmd, fullRect, ext, boxRect, bg);
        spineRenderer_.drawRect(cmd, fullRect, ext, boxRect, lg.edgeColor, 1.0f);
    }

    // Title (centered across the box); the handle box always follows a
    // labelspacing sep below the title area (mpl VPacker sep).
    float contentTop = boxY + pad;
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, titleScale);
        drawRichText(cmd, fullRect, lg.title,
                     boxX + boxW / 2.0f - m.width / 2.0f,
                     contentTop + m.ascent, style.textColor, titleScale);
    }
    contentTop += titleH + rowSep;

    const auto labelColor = lg.labelColor.value_or(style.textColor);

    // mpl HPacker align="baseline": the first items' baselines align
    // across columns, so the tallest first item sets the shared line.
    float firstAbove = 0.0f;
    for (int c = 0; c < cols; ++c)
        firstAbove = std::max(firstAbove, im[c * rows].above);

    // Draw each entry: handle (line/marker) + text label. Each column
    // packs its own items top-down with labelspacing between them.
    for (int c = 0; c < cols; ++c) {
        float colX = boxX + pad;
        for (int j = 0; j < c; ++j) colX += colW[j] + colGap;
        const float markerSize = fontPx;
        float baseline = contentTop + firstAbove;
        int prev = -1;
        for (int r = 0; r < rows; ++r) {
            const int i = c * rows + r;
            if (i >= n) break;
            if (prev >= 0)
                baseline += im[prev].below + rowSep + im[i].above;
            prev = i;
            // Handle box: hAbove above the baseline, hBelow below.
            const float midY = baseline - hAbove + hBoxH * 0.5f;
            const auto& e = entries[i];

        if (e.marker == plot::LegendMarker::Line) {
            plot::Point2D pts[] = {{colX, midY}, {colX + handleW, midY}};
            spineRenderer_.drawLineStrip(cmd, fullRect, ext, pts, e.color, 2.0f);
        } else if (e.marker == plot::LegendMarker::Circle) {
            // Filled disc (octagon fan) centered in the handle area.
            const float cx = colX + handleW / 2.0f, radius = markerSize / 2.0f;
            std::vector<plot::Point2D> fan;
            fan.reserve(3 * 8);
            for (int k = 0; k < 8; ++k) {
                const float a0 = k * 0.78539816f, a1 = (k + 1) * 0.78539816f;
                fan.push_back({cx, midY});
                fan.push_back({cx + radius * std::cos(a0),
                               midY + radius * std::sin(a0)});
                fan.push_back({cx + radius * std::cos(a1),
                               midY + radius * std::sin(a1)});
            }
            spineRenderer_.drawTriangles(cmd, fullRect, ext, fan, e.color);
        } else {
            // Filled square centered in the handle area.
            const float sx = colX + (handleW - markerSize) / 2.0f;
            spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                {int32_t(sx), int32_t(midY - markerSize / 2.0f),
                 uint32_t(markerSize), uint32_t(markerSize)},
                e.color);
        }

            // Label text: baseline at the item's shared baseline.
            drawRichText(cmd, fullRect, e.label,
                         colX + handleW + textGap, baseline,
                         labelColor, scale);
        }
    }
}

void Renderer::renderFrame(plot::Figure& figure) {
    renderFrameSubset(figure, DrawSubset::All);
    // Lazily-rasterized glyphs (first CJK/fallback-face characters in a
    // frame) grow the CPU atlas mid-pass — re-upload and repaint once.
    if (textReady_ && textRenderer_.atlasDirty()) {
        auto& ctx = backend_.context();
        textRenderer_.syncAtlas(ctx.device.graphicsQueue(),
                                ctx.graphicsPool.handle());
        renderFrameSubset(figure, DrawSubset::All);
    }
}

bool Renderer::blitCaptureBackground(plot::Figure& figure) {
    renderFrameSubset(figure, DrawSubset::StaticOnly);
    return backend_.blitCapture();
}

void Renderer::blitDrawAnimated(plot::Figure& figure) {
    renderFrameSubset(figure, DrawSubset::AnimatedOnly);
}

void Renderer::renderFrameSubset(plot::Figure& figure, DrawSubset subset) {
    auto ext = backend_.extent();
    figure.layout(plot::Extent2D{ext.width, ext.height});

    auto cmd = subset == DrawSubset::AnimatedOnly
                   ? backend_.beginFrameLoad()
                   : backend_.beginFrame();
    textRenderer_.resetScratch();
    spineRenderer_.resetScratch();

    // Figure patch (figure.facecolor) fills the canvas under everything.
    const auto& figFc = figure.style().faceColor;
    if (subset != DrawSubset::AnimatedOnly && spineInited_ && figFc.a > 0.0f) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        plot::Rect2D canvas{0, 0, ext.width, ext.height};
        spineRenderer_.drawFilledRect(cmd, fullRect, ext, canvas, figFc);
    }

    for (auto& p : figure.placements()) {
        plot::Rect2D rect = p.axes->rect;
        vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}};

        const bool gridOn = p.axes->style().xAxis.grid ||
                            p.axes->style().yAxis.grid;

        // Axes facecolor patch (matplotlib axes.facecolor).
        if (subset != DrawSubset::AnimatedOnly && spineInited_ &&
            p.axes->style().faceColor.a > 0.0f)
            spineRenderer_.drawFilledRect(cmd, vrect, ext, rect,
                                          p.axes->style().faceColor);

        const bool polar = p.axes->projection().kind ==
                           plot::ProjectionKind::Polar;

        // Draw tick-aligned grid lines (per-axis enable). axisBelow
        // selects whether the grid sits under or over the plot artists.
        // Polar axes draw radial spokes + r-circles instead.
        if (subset != DrawSubset::AnimatedOnly && gridOn &&
            p.axes->style().axisBelow)
            polar ? drawPolarGrid(cmd, *p.axes, rect)
                  : drawGrid(cmd, *p.axes, rect);

        // Draw plot layers in zorder, filtered by the blit subset.
        for (auto* plot : p.axes->drawOrder()) {
            if (subset == DrawSubset::StaticOnly && plot->animated) continue;
            if (subset == DrawSubset::AnimatedOnly && !plot->animated) continue;
            const_cast<plot::IPlot*>(plot)->draw(cmd, *this, *p.axes, rect);
        }

        if (subset != DrawSubset::AnimatedOnly && gridOn &&
            !p.axes->style().axisBelow)
            polar ? drawPolarGrid(cmd, *p.axes, rect)
                  : drawGrid(cmd, *p.axes, rect);

        if (subset == DrawSubset::AnimatedOnly) continue;

        // 3D plots (matplotlib projection="3d") draw no 2D spine
        // rectangle — mplot3d renders its own box/panes instead.
        bool has3D = std::ranges::any_of(p.axes->drawOrder(),
            [](const plot::IPlot* pl) { return pl->is3D(); });

        // Draw axis spines and tick marks. Polar axes get a circular
        // frame plus theta/r labels instead of rectilinear furniture.
        if (polar) drawPolarSpineAndLabels(cmd, *p.axes, rect);
        else if (!has3D) drawSpines(cmd, *p.axes, rect);

        // Draw text (axis labels, tick labels, title).
        if (textInited_ && textReady_) {
            drawText(cmd, *p.axes, rect);
        }

        // Draw text annotations and arrow annotations.
        if (textInited_ && textReady_) {
            drawAnnotations(cmd, *p.axes, rect);
        }

        // Draw legend (if enabled).
        drawLegend(cmd, *p.axes, rect);

        // Draw anchored size bars.
        if (textInited_ && textReady_)
            drawSizeBars(cmd, *p.axes, rect);

        // Draw anchored text boxes.
        if (textInited_ && textReady_)
            drawAnchoredTexts(cmd, *p.axes, rect);

        // Draw inset-zoom indicators (connectors span the canvas).
        drawInsetIndicators(cmd, *p.axes, rect);

        // Draw colorbar (if enabled).
        drawColorbar(cmd, *p.axes, rect);
    }

    // Figure suptitle at top center (mirrors VectorRenderer).
    const auto& ft = figure.style().title;
    if (subset != DrawSubset::AnimatedOnly && textInited_ && textReady_ &&
        !ft.text.empty()) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        float scale = ft.font.size / 12.0f;
        auto m = measureRichText(ft.text, scale);
        drawRichText(cmd, fullRect, ft.text,
                     ext.width * 0.5f - m.width * 0.5f,
                     m.ascent + 2.0f, ft.color, scale,
                     ft.font.rotation, plot::HAlign::Center);
        if (ft.weight == "bold" || ft.font.weight == "bold")
            drawRichText(cmd, fullRect, ft.text,
                         ext.width * 0.5f - m.width * 0.5f + 0.6f,
                         m.ascent + 2.0f, ft.color, scale,
                         ft.font.rotation, plot::HAlign::Center);
    }

    // Interactive overlays (§11): widgets + nav zoom rubber-band.
    if (subset != DrawSubset::AnimatedOnly && spineInited_) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        Painter painter{cmd, *this, fullRect};
        for (auto& w : figure.widgets()) w->draw(painter);
        if (figure.navCreated() && figure.nav().hasZoomRect()) {
            auto [a, b] = figure.nav().zoomRect();
            plot::Rect2D zr{std::min(a.x, b.x), std::min(a.y, b.y),
                            std::abs(b.x - a.x), std::abs(b.y - a.y)};
            spineRenderer_.drawRect(cmd, fullRect, ext, zr,
                                    plot::Color{0.0f, 0.0f, 0.0f, 0.8f}, 1.0f);
        }
    }
    backend_.endFrame();
}

void Renderer::drawColorbar(vk::CommandBuffer cmd, const plot::Axes& axes,
                            plot::Rect2D rect) {
    if (!spineInited_ || !textReady_) return;
    const auto& style = axes.style();
    if (!style.colorbar.visible) return;

    // The colorbar is tied to the first colormap-mapped plot's value range
    // (matplotlib: the mappable's norm). Fall back to the z viewport for
    // 3D-style plots that don't expose a scalar range.
    float valueMin = 0.0f, valueMax = 1.0f;
    bool hasRange = false;
    for (const auto& plot : axes.plots()) {
        if (auto vr = plot->valueRange(); vr && vr->valid()) {
            valueMin = vr->min;
            valueMax = vr->max;
            hasRange = true;
            break;
        }
    }
    if (!hasRange) {
        const auto& vp = axes.viewport();
        if (vp.z.span() > 0) {
            valueMin = vp.z.min;
            valueMax = vp.z.max;
            hasRange = true;
        }
    }
    if (!hasRange) return;

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Layout: vertical color strip to the right of the axes. mpl
    // make_axes places the cax in the right `fraction` slice of the
    // pre-shrink parent box (tracked on the axes by Figure layout);
    // for axes that didn't shrink (insets, overlays) fall back to a
    // region just outside the axes rect.
    const auto& cbs = style.colorbar;
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
    // mpl: cax box is the region shrunk vertically by `shrink` and
    // anchored (0, 0.5); set_box_aspect then narrows the width to
    // height/aspect when the region is wider (aspect=20 default).
    float stripH = float(rect.height) * cbs.shrink;
    float stripY = float(rect.y) + (float(rect.height) - stripH) * 0.5f;
    float stripW = cbs.width > 0.0f
        ? cbs.width
        : std::min(regionW, stripH / cbs.aspect);
    bool extMin = (cbs.extend == "min" || cbs.extend == "both");
    bool extMax = (cbs.extend == "max" || cbs.extend == "both");
    float extH = stripW * 0.6f;   // triangular extension height
    float bodyY0 = stripY + (extMax ? extH : 0.0f);
    float bodyY1 = stripY + stripH - (extMin ? extH : 0.0f);
    float bodyH = bodyY1 - bodyY0;

    const auto& cmap = plot::Colormap::byName(style.colorbar.colormap);
    auto sampleAt = [&](float t) -> plot::Color {
        if (cbs.norm) {
            float v = valueMin + t * (valueMax - valueMin);
            return cmap.sample((*cbs.norm)(v));
        }
        return cmap.sample(t);
    };

    if (cbs.orientation == "horizontal") {
        // mpl make_axes horizontal: strip fills the bottom region slice;
        // thickness = min(region height, stripW/aspect); ticks below.
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

        // Left-to-right gradient: min at left, max at right.
        float segW = bodyW / 64.0f;
        for (uint32_t i = 0; i < 64; ++i) {
            float t = float(i) / 63.0f;
            spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                {int32_t(bodyX0 + i * segW), int32_t(stripY),
                 uint32_t(segW) + 1, uint32_t(stripH)},
                sampleAt(t));
        }
        vk::Extent2D res2{ext.width, ext.height};
        if (extMin) {   // left-pointing triangle
            plot::Point2D tri[3] = {
                {bodyX0, stripY}, {bodyX0, stripY + stripH},
                {bodyX0 - extW, stripY + stripH * 0.5f}};
            spineRenderer_.drawTriangles(cmd, fullRect, res2, tri,
                                         sampleAt(0.0f));
        }
        if (extMax) {   // right-pointing triangle
            plot::Point2D tri[3] = {
                {bodyX1, stripY}, {bodyX1, stripY + stripH},
                {bodyX1 + extW, stripY + stripH * 0.5f}};
            spineRenderer_.drawTriangles(cmd, fullRect, res2, tri,
                                         sampleAt(1.0f));
        }
        spineRenderer_.drawRect(cmd, fullRect, ext,
            {int32_t(bodyX0), int32_t(stripY),
             uint32_t(bodyW), uint32_t(stripH)},
            style.colorbar.edgeColor, 1.0f);

        auto hticks = autoTicks(valueMin, valueMax, 8);
        float hStep = autoTickStep(valueMin, valueMax, 8);
        for (float tick : hticks) {
            float t = cbs.norm ? (*cbs.norm)(tick)
                               : (tick - valueMin) / (valueMax - valueMin);
            float x = bodyX0 + t * bodyW;
            plot::Point2D tickPts[2] = {
                {x, stripY + stripH}, {x, stripY + stripH + 4.0f}};
            spineRenderer_.drawLineStrip(cmd, fullRect, ext, tickPts,
                                         style.colorbar.edgeColor, 1.0f);
            std::string label = formatTick(tick, hStep);
            textRenderer_.draw(cmd, fullRect, label,
                               x - 8.0f, stripY + stripH + 14.0f,
                               style.colorbar.labelColor);
        }
        return;
    }

    // Draw the color strip as a series of horizontal segments.
    uint32_t segments = 64;
    float segH = bodyH / segments;
    for (uint32_t i = 0; i < segments; ++i) {
        // Max value at the top of the strip (matplotlib orientation).
        float t = 1.0f - float(i) / float(segments - 1);
        auto color = sampleAt(t);
        float y = bodyY0 + i * segH;
        spineRenderer_.drawFilledRect(cmd, fullRect, ext,
            {int32_t(stripX), int32_t(y), uint32_t(stripW), uint32_t(segH) + 1},
            color);
    }

    // Extend triangles (matplotlib colorbar extend=...). Min extension at
    // the bottom, max at the top; colored with the strip's end color.
    vk::Extent2D res{ext.width, ext.height};
    if (extMin) {
        // Downward-pointing triangle under the strip.
        plot::Point2D tri[3] = {
            {stripX, bodyY1}, {stripX + stripW, bodyY1},
            {stripX + stripW * 0.5f, bodyY1 + extH}};
        spineRenderer_.drawTriangles(cmd, fullRect, res, tri,
                                     sampleAt(0.0f));
    }
    if (extMax) {
        plot::Point2D tri[3] = {
            {stripX, bodyY0}, {stripX + stripW, bodyY0},
            {stripX + stripW * 0.5f, bodyY0 - extH}};
        spineRenderer_.drawTriangles(cmd, fullRect, res, tri,
                                     sampleAt(1.0f));
    }

    // Draw border around the strip body (plus extend outlines).
    spineRenderer_.drawRect(cmd, fullRect, ext,
        {int32_t(stripX), int32_t(bodyY0), uint32_t(stripW), uint32_t(bodyH)},
        style.colorbar.edgeColor, 1.0f);

    // Draw tick labels — positioned on the strip body (inside extends);
    // when a custom norm is set, tick t goes through it (matplotlib norm).
    auto ticks = autoTicks(valueMin, valueMax, 8);
    float cbStep = autoTickStep(valueMin, valueMax, 8);
    for (float tick : ticks) {
        float t = cbs.norm ? (*cbs.norm)(tick)
                           : (tick - valueMin) / (valueMax - valueMin);
        float y = bodyY0 + (1.0f - t) * bodyH;  // top = max, bottom = min
        // Draw tick mark.
        plot::Point2D tickPts[2] = {
            {stripX + stripW, y},
            {stripX + stripW + 4.0f, y},
        };
        spineRenderer_.drawLineStrip(cmd, fullRect, ext, tickPts,
                                     style.colorbar.edgeColor, 1.0f);
        // Draw label.
        std::string label = formatTick(tick, cbStep);
        textRenderer_.draw(cmd, fullRect, label,
                           stripX + stripW + 8.0f, y + 6.0f,
                           style.colorbar.labelColor);
    }
}

void Renderer::drawAnnotations(vk::CommandBuffer cmd, const plot::Axes& axes,
                                plot::Rect2D rect) {
    const auto& vp = axes.viewport();
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    plot::Extent2D figExtent{ext.width, ext.height};
    float dpi = axes.style().dpi;
    float baseFontSize = 16.0f;

    // --- Text annotations (ax.text) ---
    for (const auto& t : axes.texts()) {
        if (t.text.empty()) continue;
        auto pos = plot::toDisplay(t.x, t.y, t.coords, rect, figExtent, axes, dpi,
                                   t.xyOffsetX, t.xyOffsetY);
        pos.x += t.dragOffset.x;
        pos.y += t.dragOffset.y;
        float scale = t.fontSize;
        auto m = measureRichText(t.text, scale);
        {
            auto al = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            t.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                         uint32_t(m.width), uint32_t(m.height)};
        }
        // clipOn restricts the scissor to the axes rect.
        vk::Rect2D clipRect = t.clipOn
            ? vk::Rect2D{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}}
            : fullRect;

        // Draw background box if requested.
        if (t.bboxFaceColor.a > 0.0f) {
            auto aligned = plot::alignText(pos, t.halign, t.valign,
                                           m.width, m.height, m.ascent);
            float pad = t.bboxPadding;
            plot::Rect2D bbox{
                static_cast<int32_t>(aligned.x - pad),
                static_cast<int32_t>(aligned.y - m.ascent - pad),
                static_cast<uint32_t>(m.width + 2 * pad),
                static_cast<uint32_t>(m.height + 2 * pad)};
            spineRenderer_.drawFilledRect(cmd, clipRect, backend_.extent(),
                                          bbox, t.bboxFaceColor);
            if (t.bboxEdgeColor.a > 0.0f) {
                spineRenderer_.drawRect(cmd, clipRect, backend_.extent(),
                                        bbox, t.bboxEdgeColor, 1.0f);
            }
        }

        auto drawPos = plot::alignText(pos, t.halign, t.valign,
                                       m.width, m.height, m.ascent);
        drawRichText(cmd, clipRect, t.text,
                     drawPos.x, drawPos.y, t.color, scale, t.rotation,
                     t.halign);
    }

    // --- Arrow annotations (ax.annotate) ---
    for (const auto& a : axes.annotations()) {
        // Compute pixel positions for the data point and the text.
        auto dataPos = plot::toDisplay(a.xy[0], a.xy[1], a.xyCoords,
                                       rect, figExtent, axes, dpi);
        auto textPos = plot::toDisplay(a.xyText[0], a.xyText[1], a.xyTextCoords,
                                       rect, figExtent, axes, dpi,
                                       a.textOffsetX, a.textOffsetY);
        textPos.x += a.dragOffset.x;
        textPos.y += a.dragOffset.y;
        vk::Rect2D clipRect = a.clipOn
            ? vk::Rect2D{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}}
            : fullRect;

        // Draw arrow if requested.
        if (a.arrowStyle != plot::ArrowStyle::None) {
            // Sample the connection path (arc3/angle/arc/bar).
            auto path = plot::connectionPath(textPos, dataPos, a.connection,
                                             a.shrinkA, a.shrinkB);
            if (path.size() >= 2 && a.arrowSpec) {
                // mpl arrowstyle spec → strokes + filled marks.
                // mutationSize is in points; mpl scales by dpi/72.
                auto spec = *a.arrowSpec;
                spec.mutationSize *= dpi / 72.0f;
                auto geo = plot::buildArrowGeometry(path, spec,
                                                    a.arrowWidth);
                // Optional artist clip path (data coords → px ring).
                std::vector<plot::Point2D> ring;
                if (a.clipPath) {
                    auto subs = a.clipPath->toPolylines();
                    const plot::Path::Subpath* best = nullptr;
                    for (auto& sp : subs)
                        if (!best || sp.points.size() > best->points.size())
                            best = &sp;
                    if (best && best->points.size() >= 3)
                        for (auto p : best->points) {
                            auto f2 = axes.dataToFraction(p);
                            ring.push_back(
                                {rect.x + f2.x * float(rect.width),
                                 rect.y + (1.0f - f2.y) * float(rect.height)});
                        }
                }
                vk::Extent2D res{backend_.extent().width,
                                 backend_.extent().height};
                for (auto& s : geo.strokes) {
                    if (ring.empty()) {
                        spineRenderer_.drawLineStrip(cmd, clipRect,
                            backend_.extent(), std::span{s},
                            a.arrowColor, a.arrowWidth);
                    } else {
                        for (auto& piece : plot::clipPolylineToPolygon(
                                 std::span<const plot::Point2D>{s}, ring))
                            spineRenderer_.drawLineStrip(cmd, clipRect,
                                backend_.extent(),
                                std::span<const plot::Point2D>{piece},
                                a.arrowColor, a.arrowWidth);
                    }
                }
                for (auto& f : geo.fills) {
                    auto tris = plot::earClip(f);
                    if (!ring.empty())
                        tris = plot::clipTrianglesToPolygon(tris, ring);
                    if (!tris.empty())
                        spineRenderer_.drawTriangles(cmd, clipRect, res,
                            std::span{tris}, a.arrowColor);
                }
            } else if (path.size() >= 2) {
                spineRenderer_.drawLineStrip(cmd, clipRect, backend_.extent(),
                    std::span{path}, a.arrowColor, a.arrowWidth);

                // Arrowhead along the final segment's tangent.
                size_t n = path.size();
                float ux = path[n-1].x - path[n-2].x;
                float uy = path[n-1].y - path[n-2].y;
                float tl = std::hypot(ux, uy);
                if (tl > 1e-4f) {
                    ux /= tl; uy /= tl;
                    float endX = path[n-1].x, endY = path[n-1].y;
                    float headLen = a.arrowHeadSize;
                    float headAngle = a.arrowHeadAngle * static_cast<float>(M_PI) / 180.0f;
                    float cosA = std::cos(headAngle);
                    float sinA = std::sin(headAngle);
                    float leftX = ux * cosA - uy * sinA;
                    float leftY = ux * sinA + uy * cosA;
                    float rightX = ux * cosA + uy * sinA;
                    float rightY = -ux * sinA + uy * cosA;
                    plot::Point2D head1[2] = {
                        {endX, endY},
                        {endX - leftX * headLen, endY - leftY * headLen}
                    };
                    plot::Point2D head2[2] = {
                        {endX, endY},
                        {endX - rightX * headLen, endY - rightY * headLen}
                    };
                    spineRenderer_.drawLineStrip(cmd, clipRect,
                        backend_.extent(),
                        std::span{head1, 2}, a.arrowColor, a.arrowWidth);
                    spineRenderer_.drawLineStrip(cmd, clipRect,
                        backend_.extent(),
                        std::span{head2, 2}, a.arrowColor, a.arrowWidth);
                }
            }
        }

        // Draw the text label.
        if (!a.text.empty()) {
            float scale = a.fontSize;
            auto m = measureRichText(a.text, scale);
            {
                auto al = plot::alignText(textPos, a.halign, a.valign,
                                          m.width, m.height, m.ascent);
                a.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                             uint32_t(m.width), uint32_t(m.height)};
            }

            // Draw background box if requested.
            if (a.bboxFaceColor.a > 0.0f) {
                auto aligned = plot::alignText(textPos, a.halign, a.valign,
                                               m.width, m.height, m.ascent);
                float pad = a.bboxPadding;
                plot::Rect2D bbox{
                    static_cast<int32_t>(aligned.x - pad),
                    static_cast<int32_t>(aligned.y - m.ascent - pad),
                    static_cast<uint32_t>(m.width + 2 * pad),
                    static_cast<uint32_t>(m.height + 2 * pad)};
                if (a.boxStyle) {
                    // mpl bbox=dict(boxstyle=...): outline path in px space.
                    // mutation_size = fontsize (pt) × dpi/72, matching mpl.
                    auto bs = *a.boxStyle;
                    bs.mutationSize *= a.fontSize * 16.0f;
                    auto path = plot::boxStylePath(float(bbox.x),
                                                   float(bbox.y),
                                                   float(bbox.width),
                                                   float(bbox.height),
                                                   bs);
                    vk::Extent2D res{backend_.extent().width,
                                     backend_.extent().height};
                    for (auto& sp : path.toPolylines(24)) {
                        if (a.bboxFaceColor.a > 0.0f) {
                            auto tris = plot::earClip(sp.points);
                            if (!tris.empty())
                                spineRenderer_.drawTriangles(
                                    cmd, clipRect, res,
                                    std::span{tris}, a.bboxFaceColor);
                        }
                        if (a.bboxEdgeColor.a > 0.0f) {
                            auto ring = sp.points;
                            if (sp.closed && !ring.empty())
                                ring.push_back(ring.front());
                            spineRenderer_.drawLineStrip(
                                cmd, clipRect, backend_.extent(),
                                std::span<const plot::Point2D>{ring},
                                a.bboxEdgeColor, 1.0f);
                        }
                    }
                } else {
                    spineRenderer_.drawFilledRect(cmd, clipRect,
                                                  backend_.extent(),
                                                  bbox, a.bboxFaceColor);
                    if (a.bboxEdgeColor.a > 0.0f) {
                        spineRenderer_.drawRect(cmd, clipRect,
                                                backend_.extent(),
                                                bbox, a.bboxEdgeColor, 1.0f);
                    }
                }
            }

            auto drawPos = plot::alignText(textPos, a.halign, a.valign,
                                           m.width, m.height, m.ascent);
            drawRichText(cmd, clipRect, a.text,
                         drawPos.x, drawPos.y, a.color, scale, 0.0f,
                         a.halign);
        }
    }
}

bool Renderer::savefig(plot::Figure& figure,
                       const std::filesystem::path& path,
                       const encode::SaveOptions& options) {
    auto fmt = options.format
        ? *options.format
        : encode::formatFromPath(path).value_or(encode::ImageFormat::Png);
    switch (fmt) {
    case encode::ImageFormat::Pdf:
    case encode::ImageFormat::Svg:
    case encode::ImageFormat::Eps:
    case encode::ImageFormat::Pgf:
        return savefigVector(figure, path, options, fmt);
    default: break;
    }

    // Transparent output → clear with alpha 0 (matplotlib
    /// savefig(transparent=True)) and hide the figure patch.
    auto savedFc = figure.style().faceColor;
    if (options.transparent) {
        backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        figure.style().faceColor.a = 0.0f;
    }

    prepare(figure);
    renderFrame(figure);

    // Restore the opaque clear for subsequent frames.
    if (options.transparent) {
        backend_.setClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        figure.style().faceColor = savedFc;
    }

    auto pixels = backend_.readbackRgba8();
    if (pixels.empty()) return false;
    auto ext = backend_.extent();
    return encode::saveImage(pixels, ext.width, ext.height, path, options);
}

bool Renderer::savefigVector(plot::Figure& figure,
                             const std::filesystem::path& path,
                             const encode::SaveOptions& options,
                             encode::ImageFormat fmt) {
    VectorFormat vf;
    switch (fmt) {
    case encode::ImageFormat::Svg: vf = VectorFormat::Svg; break;
    case encode::ImageFormat::Pdf: vf = VectorFormat::Pdf; break;
    case encode::ImageFormat::Eps: vf = VectorFormat::Eps; break;
    case encode::ImageFormat::Pgf: vf = VectorFormat::Pgf; break;
    default: return false;
    }
    prepare(figure);
    auto ext = backend_.extent();
    figure.layout(plot::Extent2D{ext.width, ext.height});

    VectorOptions vo;
    vo.facecolor = options.transparent ? plot::Color{0, 0, 0, 0}
                                       : figure.style().faceColor;
    vo.metadata = options.metadata;
    vo.dpi = options.dpi;

    VectorRenderer::MeasureFn measure;
    if (textReady_)
        measure = [this](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc);
            return text::TextMeasure{m.width, m.height, m.ascent};
        };
    // Raster fallback: draw only this run's layers for `axes` onto a
    // transparent frame and read back the whole canvas.
    VectorRenderer::RasterFn rasterize =
        [this](const plot::Axes& axes,
               std::span<const plot::IPlot* const> plots,
               std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
            backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            auto cmd = backend_.beginFrame();
            spineRenderer_.resetScratch();
            textRenderer_.resetScratch();
            for (auto* p : plots)
                const_cast<plot::IPlot*>(p)->draw(cmd, *this, axes, axes.rect);
            backend_.endFrame();
            backend_.setClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            rgba = backend_.readbackRgba8();
            auto e = backend_.extent();
            w = e.width; h = e.height;
            return !rgba.empty();
        };
    VectorRenderer vr{std::move(measure), std::move(rasterize),
                      plot::Extent2D{ext.width, ext.height}};
    auto canvas = vectorCanvas(vf, float(ext.width), float(ext.height), vo);
    if (!canvas) return false;
    vr.render(figure, *canvas);
    return canvas->finish(path);
}

bool Renderer::saveAnimation(plot::Animation& anim,
                             const std::filesystem::path& path, double fps,
                             std::string_view writerName) {
    auto w = encode::createMovieWriter(writerName, path);
    if (!w) return false;
    auto ext = backend_.extent();
    if (!w->open(path, ext.width, ext.height, fps)) return false;
    size_t n = anim.frameCount();
    // Blit path (mpl blit=True): snapshot the static background once,
    // then per frame restore it and draw only `animated` artists.
    bool useBlit = false;
    if (anim.blit && n > 0) {
        anim.drawFrame(0);
        prepare(anim.figure());
        useBlit = blitCaptureBackground(anim.figure());
    }
    for (size_t i = 0; i < n; ++i) {
        anim.drawFrame(i);
        prepare(anim.figure());
        if (useBlit) blitDrawAnimated(anim.figure());
        else         renderFrame(anim.figure());
        auto px = backend_.readbackRgba8();
        if (px.empty() || !w->writeFrame(px)) return false;
    }
    return w->finish();
}

std::string Renderer::toJsHtml(plot::Animation& anim, double fps) {
    auto png = encode::createCpuEncoder(encode::ImageFormat::Png);
    if (!png) return {};
    std::vector<std::vector<uint8_t>> frames;
    size_t n = anim.frameCount();
    bool useBlit = false;
    if (anim.blit && n > 0) {
        anim.drawFrame(0);
        prepare(anim.figure());
        useBlit = blitCaptureBackground(anim.figure());
    }
    for (size_t i = 0; i < n; ++i) {
        anim.drawFrame(i);
        prepare(anim.figure());
        if (useBlit) blitDrawAnimated(anim.figure());
        else         renderFrame(anim.figure());
        auto px = backend_.readbackRgba8();
        if (px.empty()) return {};
        auto ext = backend_.extent();
        auto enc = png->encode(px, ext.width, ext.height);
        if (!enc.success) return {};
        frames.push_back(std::move(enc.bytes));
    }
    auto ext = backend_.extent();
    return encode::jsHtmlFromPngFrames(frames, fps, ext.width, ext.height);
}

std::string Renderer::toHtml5Video(plot::Animation& anim, double fps) {
    if (encode::FFMpegWriter::available()) {
        auto tmp = std::filesystem::temp_directory_path() / "volcano_anim.mp4";
        if (saveAnimation(anim, tmp, fps, "ffmpeg")) {
            FILE* f = std::fopen(tmp.c_str(), "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END);
                long len = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                std::vector<uint8_t> bytes;
                bytes.resize(size_t(len));
                std::fread(bytes.data(), 1, bytes.size(), f);
                std::fclose(f);
                std::filesystem::remove(tmp);
                return encode::html5VideoFromMovie(bytes);
            }
        }
    }
    return toJsHtml(anim, fps); // fallback: JS player
}


void Renderer::drawSizeBars(vk::CommandBuffer cmd, const plot::Axes& axes,
                            plot::Rect2D rect) {
    if (axes.sizeBars().empty()) return;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    const float dpi = axes.style().dpi;
    auto measureFn = [&](std::string_view t, float scale) {
        auto m = measureRichText(t, scale);
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    auto toI = [](plot::Rect2Df r) {
        return plot::Rect2D{int32_t(std::lround(r.x)),
                            int32_t(std::lround(r.y)),
                            uint32_t(std::lround(r.w)),
                            uint32_t(std::lround(r.h))};
    };
    for (const auto& sb : axes.sizeBars()) {
        auto L = plot::layoutSizeBar(sb, axes, rect, {ext.width, ext.height},
                                     dpi, measureFn);
        if (!L.valid) continue;
        if (sb.frameon)
            spineRenderer_.drawFilledRect(cmd, fullRect, ext, toI(L.box),
                                          sb.frameFaceColor);
        if (L.fill)
            spineRenderer_.drawFilledRect(cmd, fullRect, ext, toI(L.bar),
                                          sb.color);
        else
            spineRenderer_.drawRect(cmd, fullRect, ext, toI(L.bar),
                                    sb.color, 1.0f);
        if (!sb.label.empty())
            drawRichText(cmd, fullRect, sb.label, L.labelBaseline.x,
                         L.labelBaseline.y, sb.color, sb.fontSize);
    }
}


void Renderer::drawInsetIndicators(vk::CommandBuffer cmd,
                                   const plot::Axes& axes,
                                   plot::Rect2D rect) {
    if (axes.insetIndicators().empty()) return;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    plot::Extent2D figExtent{ext.width, ext.height};
    for (const auto& ind : axes.insetIndicators()) {
        auto L = plot::layoutInsetIndicator(ind, axes, rect, figExtent);
        if (!L.valid) continue;
        auto edge = ind.edgeColor;
        edge.a *= ind.alpha;
        auto face = ind.faceColor;
        face.a *= ind.alpha;
        if (face.a > 0.0f)
            spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                {int32_t(std::lround(L.rect.x)), int32_t(std::lround(L.rect.y)),
                 uint32_t(std::lround(L.rect.w)),
                 uint32_t(std::lround(L.rect.h))}, face);
        spineRenderer_.drawRect(cmd, fullRect, ext,
            {int32_t(std::lround(L.rect.x)), int32_t(std::lround(L.rect.y)),
             uint32_t(std::lround(L.rect.w)),
             uint32_t(std::lround(L.rect.h))}, edge, ind.lineWidth);
        for (int i = 0; i < 4; ++i) {
            if (!L.connVisible[i]) continue;
            plot::Point2D seg[2] = {L.connectors[i].first,
                                    L.connectors[i].second};
            spineRenderer_.drawLineStrip(cmd, fullRect, ext,
                std::span{seg}, edge, ind.lineWidth);
        }
    }
}


void Renderer::drawAnchoredTexts(vk::CommandBuffer cmd,
                                 const plot::Axes& axes,
                                 plot::Rect2D rect) {
    if (axes.anchoredTexts().empty()) return;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    auto measureFn = [&](std::string_view t, float scale) {
        auto m = measureRichText(t, scale);
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& at : axes.anchoredTexts()) {
        auto L = plot::layoutAnchoredText(at, axes, rect, measureFn);
        if (!L.valid) continue;
        if (at.frameon) {
            plot::Rect2D box{int32_t(std::lround(L.box.x)),
                             int32_t(std::lround(L.box.y)),
                             uint32_t(std::lround(L.box.w)),
                             uint32_t(std::lround(L.box.h))};
            spineRenderer_.drawFilledRect(cmd, fullRect, ext, box,
                                          at.frameFaceColor);
            spineRenderer_.drawRect(cmd, fullRect, ext, box,
                                    at.frameEdgeColor, 1.0f);
        }
        size_t i = 0;
        size_t start = 0;
        while (true) {
            size_t nl = at.text.find('\n', start);
            auto line = std::string_view(at.text).substr(
                start, nl == std::string_view::npos
                           ? std::string_view::npos : nl - start);
            drawRichText(cmd, fullRect, line, L.lineBaselines[i].x,
                         L.lineBaselines[i].y, at.color, at.fontSize);
            ++i;
            if (nl == std::string_view::npos) break;
            start = nl + 1;
        }
    }
}

} // namespace volcano::render
