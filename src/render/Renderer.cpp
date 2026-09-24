// volcano/render/Renderer.cpp
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorRenderer.hpp"
#include "volcano/render/VectorWriters.hpp"
#include <volcano/encode/GpuPngEncoder.hpp>
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
#include <cstdlib>
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

namespace {
/// vkPipelineCache blob path: $VOLCANO_CACHE_DIR, then
/// $XDG_CACHE_HOME/volcanoplot, then ~/.cache/volcanoplot. Empty = disabled.
/// Driver-specific blobs are validated by Vulkan on load, so stale or
/// foreign caches are safely ignored. Pipeline creation on lavapipe is
/// ~150 ms each — the cache pays for itself after the first run.
std::filesystem::path pipelineCacheFile() {
    std::filesystem::path dir;
    if (const char* d = std::getenv("VOLCANO_CACHE_DIR"); d && d[0]) {
        dir = d;
    } else if (const char* x = std::getenv("XDG_CACHE_HOME"); x && x[0]) {
        dir = std::filesystem::path(x) / "volcanoplot";
    } else if (const char* h = std::getenv("HOME"); h && h[0]) {
        dir = std::filesystem::path(h) / ".cache" / "volcanoplot";
    } else {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    return dir / "pipeline-cache.bin";
}
} // namespace

Renderer::Renderer(backend::IBackend& backend) : backend_(backend) {
    auto& ctx = backend_.context();
    pipelineCache_ = std::make_unique<core::PipelineCache>(
        ctx.device.handle(), pipelineCacheFile());
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
        instancedPathRenderer_.init(ctx.device.handle(), ctx.allocator.handle(),
                                    backend_.renderPass(),
                                    backend_.sampleCount(),
                                    *pipelineCache_, *descriptorPool_);
        gpuLineRenderer_.init(ctx.device.handle(), ctx.allocator.handle(),
                              *descriptorPool_, *pipelineCache_);
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
    font_face* serif = textRenderer_.serifFace();
    text::MeasureFn alt;
    if (mathFontset_ == text::MathFontset::DejaVuSerif && serif)
        alt = [this, serif](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc, serif);
            return text::TextMeasure{m.width, m.height, m.ascent};
        };
    auto lay = text::layoutMathText(text, scale,
        [this](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc);
            return text::TextMeasure{m.width, m.height, m.ascent};
        }, mathFontset_, alt);
    return {lay.width, lay.ascent + lay.descent, lay.ascent};
}

text::TextRenderer::FaceMatch
Renderer::richTextFace(const plot::FontProperties& font) {
    return textRenderer_.faceFor(font.family, font.style, font.weight);
}

void Renderer::drawRichText(vk::CommandBuffer cmd, vk::Rect2D scissor,
                            std::string_view text, float x, float y,
                            plot::Color color, float scale, float rotation,
                            plot::HAlign lineAlign,
                            const plot::FontProperties* font) {
    if (!textReady_ || text.empty()) return;
    font_face* face = font ? richTextFace(*font).face : nullptr;
    if (!text::containsMath(text)) {
        textRenderer_.draw(cmd, scissor, text, x, y, color, scale,
                           rotation, lineAlign, face);
        return;
    }
    // Layout the math segments and emit each positioned run/rule.
    font_face* serif = textRenderer_.serifFace();
    text::MeasureFn alt;
    if (mathFontset_ == text::MathFontset::DejaVuSerif && serif)
        alt = [this, serif](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc, serif);
            return text::TextMeasure{m.width, m.height, m.ascent};
        };
    auto lay = text::layoutMathText(text, scale,
        [this](std::string_view s, float sc) {
            auto m = textRenderer_.measureText(s, sc);
            return text::TextMeasure{m.width, m.height, m.ascent};
        }, mathFontset_, alt);
    float cosR = std::cos(rotation), sinR = std::sin(rotation);
    auto rot = [&](float px, float py) {
        return plot::Point2D{x + px * cosR - py * sinR,
                             y + px * sinR + py * cosR};
    };
    for (const auto& r : lay.runs) {
        auto p = rot(r.x, r.baseline);
        font_face* rface = (r.face == 1) ? serif : face;
        textRenderer_.draw(cmd, scissor, r.text, p.x, p.y,
                           color, scale * r.scale, rotation,
                           plot::HAlign::Left, rface);
    }
    for (const auto& rl : lay.rules) {
        auto p0 = rot(rl.x0, rl.y0);
        auto p1 = rot(rl.x1, rl.y0);
        plot::Point2D pts[2] = {p0, p1};
        spineRenderer_.drawLineStrip(cmd, scissor, backend_.extent(),
                                     std::span{pts, 2}, color, rl.thickness);
    }
}

void Renderer::drawRichTextFx(vk::CommandBuffer cmd, vk::Rect2D scissor,
                              std::span<const plot::PathEffect> fxs,
                              std::string_view text, float x, float y,
                              plot::Color color, float scale,
                              float rotation, plot::HAlign lineAlign,
                              float dpi,
                              const plot::FontProperties* font) {
    if (!textReady_ || text.empty()) return;
    auto normal = [&] {
        drawRichText(cmd, scissor, text, x, y, color, scale, rotation,
                     lineAlign, font);
    };
    if (fxs.empty()) { normal(); return; }
    for (const auto& fx : fxs) {
        auto off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case plot::PathEffect::Kind::Normal:
            normal();
            break;
        case plot::PathEffect::Kind::Stroke: {
            // Bitmap-atlas text can't stroke glyph outlines — draw the
            // text at a disk of offsets in the foreground color, which
            // produces the same visual outline once the normal pass
            // overpaints the interior.
            plot::Color fc = fx.foreground.value_or(color);
            float rad = fx.strokeWidthPx(1.0f, dpi) * 0.5f;
            constexpr int kDirs = 8;
            for (int i = 0; i < kDirs; ++i) {
                float a = float(i) * 6.2831853f / float(kDirs);
                drawRichText(cmd, scissor, text,
                             x + off.x + rad * std::cos(a),
                             y + off.y + rad * std::sin(a),
                             fc, scale, rotation, lineAlign, font);
            }
            break;
        }
        case plot::PathEffect::Kind::LineShadow:
        case plot::PathEffect::Kind::PatchShadow:
            drawRichText(cmd, scissor, text, x + off.x, y + off.y,
                         fx.shadowFor(color), scale, rotation, lineAlign,
                         font);
            break;
        }
        if (fx.thenNormal) normal();
    }
}

void Renderer::drawText(vk::CommandBuffer cmd, const plot::Axes& axes,
                        plot::Rect2D rect) {
    if (!textReady_) return;

    const auto& style = axes.style();
    // mpl: pt→px conversions use the *figure* dpi.
    const float figDpi =
        axes.figure() ? axes.figure()->dpi() : style.dpi;
    // Skip text rendering if axes are not visible (e.g. flat test style).
    // mpl axison=False still draws the title — it's a figure artist,
    // not part of the axis furniture.
    if ((!axes.axison() ||
         (!style.xAxis.visible && !style.yAxis.visible)) &&
        style.title.text.empty()) {
        return;
    }
    // Text scale: element font sizes are in points at style.dpi; the
    // atlas renders 16px at scale 1.
    // mpl: each text element has its own size rcParam — title
    // (axes.titlesize='large'), axis labels (axes.labelsize='medium'),
    // tick labels (xtick/ytick.labelsize='medium'). Honor the
    // per-element FontProperties.size (set via fontsize/labelsize kwargs
    // or style presets like seaborn contexts).
    const float titleScale =
        style.title.font.size * figDpi / (72.0f * 16.0f);
    const float xTickScale =
        style.xAxis.tickFont.size * figDpi / (72.0f * 16.0f);
    const float yTickScale =
        style.yAxis.tickFont.size * figDpi / (72.0f * 16.0f);
    const float xLabelScale =
        style.xAxis.labelFont.size * figDpi / (72.0f * 16.0f);
    const float yLabelScale =
        style.yAxis.labelFont.size * figDpi / (72.0f * 16.0f);
    // mpl: tick labels and the offset text use the axis color unless
    // tick_params labelcolor/colors overrode it.
    const auto xTickColor =
        style.xAxis.ticks.labelColor.value_or(style.xAxis.color);
    const auto yTickColor =
        style.yAxis.ticks.labelColor.value_or(style.yAxis.color);

    // Use the full framebuffer as the scissor rect so text outside the
    // axes rect (labels, title) is not clipped.
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Gap between a tick mark and its label comes from the axis's tick
    // pad (matplotlib xtick.major.pad=3.5pt / xtick.minor.pad=3.4pt).

    // --- Title ---
    if (!style.title.text.empty()) {
        auto m = measureRichText(style.title.text, titleScale);
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        // Position above the axes: text bottom at rect.y - padding.
        // text bottom = y + descent = y + (height - ascent)
        // So y = rect.y - pad - (height - ascent) = rect.y - pad - height + ascent
        float cy = float(rect.y) - style.title.pad * figDpi / 72.0f -
                   m.height + m.ascent;
        auto tm = richTextFace(style.title.font);
        drawRichText(cmd, fullRect,
            style.title.text, cx, cy, style.title.color, titleScale,
            style.title.font.rotation, plot::HAlign::Center,
            &style.title.font);
        // Faux bold: second pass offset ~0.6px when no real bold face.
        if ((style.title.weight == "bold" ||
             style.title.font.weight == "bold") && !tm.bold)
            drawRichText(cmd, fullRect,
                style.title.text, cx + 0.6f, cy, style.title.color,
                titleScale,
                style.title.font.rotation, plot::HAlign::Center,
                &style.title.font);
    }

    // mpl axison=False: no tick/axis labels (axis objects removed from
    // the draw list); the title above is still emitted.
    if (!axes.axison()) return;

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
                                float(rect.width), style.xAxis.tickFont.size,
                                figDpi, false);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, xTicks, style, axes.xscale(),
                                             vp.x.min, vp.x.max,
                                             defaultFmt, strFmt, logFmt);
        const auto& xf = axes.xFurniture();
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * figDpi / 72.0f;
        // mpl label_outer: tick labels (and the offset text) are hidden
        // on axes that are not on the last row.
        const bool labelsHidden = axes.xTickLabelsHidden();
        // mpl: tick labels and the axis label ride on the spine — a
        // repositioned bottom/top spine moves them (set_position).
        auto edgeFor = [&](bool top) {
            float e = top ? float(rect.y) : float(rect.y + rect.height);
            const auto& spec = axes.spines().side(top ? "top" : "bottom");
            if (spec.positionSet)
                e = axes.spineLine(top ? "top" : "bottom", rect).pos;
            return e;
        };
        float tickLabelH = 0.0f;
        int i = 0;
        auto drawLabelsSide = [&](bool top) {
            float edge = edgeFor(top);
            float d = top ? -1.0f : 1.0f;
            float tickEnd = edge + d * outLen;
            for (float tick : xTicks) {
                float px = rect.x + axes.dataToFraction({tick, 0.0f}).x * rect.width;
                if (px < rect.x || px > rect.x + rect.width) { ++i; continue; }
                auto label = tickLabel(tc, *fmt, tick, i++);
                if (label.empty() || tc.hiddenLabels.contains(i - 1))
                    continue;
                auto m = measureRichText(label, xTickScale);
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
                    // Horizontal center at px; label outside the tick mark.
                    x = px - m.width * 0.5f;
                }
                drawRichText(cmd, fullRect, label, x, y, xTickColor, xTickScale,
                             rot, plot::HAlign::Left,
                             &style.xAxis.tickFont);
            }
            // Minor tick labels: an explicit minor formatter, or mpl's
            // default log-axis minor labels (LogFormatterSciNotation with
            // minor_thresholds suppression on crowded axes).
            if (logMinorLabels(tc, axes.xscale())) {
                auto minor = axisMinorTicks(tc, axes.xscale(), vp.x.min,
                                            vp.x.max, xTicks);
                plot::LogFormatterSciNotation defMinorFmt;
                plot::Formatter* mfmt = tc.minorFormatter
                                            ? tc.minorFormatter.get()
                                            : &defMinorFmt;
                mfmt->setViewInterval(vp.x.min, vp.x.max);
                mfmt->setLocs(minor);
                float mScale = xTickScale;
                float mEnd = edge + d * tc.minorSize * figDpi / 72.0f *
                                (1.0f - tickInFrac(tc));
                float mGap = tc.minorPad * figDpi / 72.0f;
                int mi = 0;
                for (float t : minor) {
                    float px = rect.x + axes.dataToFraction({t, 0.0f}).x * rect.width;
                    if (px < rect.x || px > rect.x + rect.width) { ++mi; continue; }
                    auto label = fmtLabel(*mfmt, t, mi++);
                    if (label.empty() ||
                        tc.hiddenMinorLabels.contains(mi - 1))
                        continue;
                    auto m = measureRichText(label, mScale);
                    float x = px - m.width * 0.5f;
                    float y = top ? mEnd - mGap - m.height + m.ascent * 0.5f
                                  : mEnd + mGap + m.ascent;
                    drawRichText(cmd, fullRect, label, x, y, xTickColor,
                                 mScale, 0.0f, plot::HAlign::Left,
                                 &style.xAxis.tickFont);
                }
            }
        };
        if (!labelsHidden) {
            if (xf.labelsNear) drawLabelsSide(false);
            if (xf.labelsFar) drawLabelsSide(true);
        }
        // Offset/scientific text at the axis end (matplotlib "+1e4"),
        // on the label side (mpl offsetText follows _tick_position).
        const bool offTop = xf.labelsFar && !xf.labelsNear;
        auto off = fixMinus(fmt->offsetText());
        if (!labelsHidden && !off.empty()) {
            auto m = measureRichText(off, xTickScale * 0.8f);
            float x = rect.x + rect.width - m.width * 0.5f;
            float edge = edgeFor(offTop);
            float d = offTop ? -1.0f : 1.0f;
            float tickEnd = edge + d * outLen;
            float y = offTop ? tickEnd - kTickSpacing - m.height + m.ascent
                             : tickEnd + kTickSpacing + m.height + m.ascent * 0.2f;
            drawRichText(cmd, fullRect, off, x, y, xTickColor, xTickScale * 0.8f,
                         0.0f, plot::HAlign::Left, &style.xAxis.tickFont);
        }

        // --- Secondary x axis (mpl secondary_xaxis): top-side tick
        // labels in transformed units via the inverse map.
        if (auto sec = axes.secondaryX()) {
            float slo = sec->forward(vp.x.min), shi = sec->forward(vp.x.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.width), style.xAxis.tickFont.size,
                                    figDpi, false);
            plot::ScalarFormatter sFmt;
            sFmt.setLocs(sTicks);
            for (float st : sTicks) {
                float v = sec->inverse(st);
                float px = rect.x + axes.dataToFraction({v, 0.0f}).x *
                           rect.width;
                if (px < rect.x || px > rect.x + rect.width) continue;
                auto label = sFmt.format(st, 0);
                if (label.empty()) continue;
                auto m = measureRichText(label, xTickScale);
                drawRichText(cmd, fullRect, label, px - m.width * 0.5f,
                             rect.y - outLen - kTickSpacing - m.height +
                                 m.ascent, xTickColor, xTickScale, 0.0f,
                             plot::HAlign::Left, &style.xAxis.tickFont);
            }
        }

        // --- X axis label --- centered below the tick labels (or above
        // when the label position is 'top'), using the measured
        // tick-label depth.
        const bool labelTop = xf.labelFar;
        if (!style.xAxis.label.empty()) {
            auto m = measureRichText(style.xAxis.label, xLabelScale);
            float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
            float edge = edgeFor(labelTop);
            float d = labelTop ? -1.0f : 1.0f;
            // mpl: the label clears the tick labels on its own side.
            float sideLabelH = (labelTop ? xf.labelsFar : xf.labelsNear)
                                   ? tickLabelH : 0.0f;
            float cy = edge + d * (outLen + kTickSpacing + sideLabelH +
                                   style.xAxis.labelPad * figDpi / 72.0f +
                                   axes.xLabelShiftPx) +
                       m.ascent - (labelTop ? m.height : 0.0f);
            drawRichText(cmd, fullRect,
                style.xAxis.label, cx, cy, style.xAxis.labelColor, xLabelScale,
                style.xAxis.labelFont.rotation, plot::HAlign::Center,
                &style.xAxis.labelFont);
        }
    }

    if (!has3D && style.yAxis.visible) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                float(rect.height), style.yAxis.tickFont.size,
                                figDpi, true);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, yTicks, style, axes.yscale(),
                                             vp.y.min, vp.y.max,
                                             defaultFmt, strFmt, logFmt);
        const auto& yf = axes.yFurniture();
        // mpl: y tick labels + axis label follow the repositioned spine.
        auto edgeFor = [&](bool right) {
            float e = right ? float(rect.x + rect.width) : float(rect.x);
            const auto& spec = axes.spines().side(right ? "right" : "left");
            if (spec.positionSet)
                e = axes.spineLine(right ? "right" : "left", rect).pos;
            return e;
        };
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        const float kTickSpacing = tc.majorPad * figDpi / 72.0f;
        // mpl label_outer: hidden on axes that are not in the first
        // column.
        const bool labelsHidden = axes.yTickLabelsHidden();
        float tickLabelW = 0.0f;
        int i = 0;
        auto drawLabelsSide = [&](bool right) {
            float edge = edgeFor(right);
            float d = right ? 1.0f : -1.0f;
            float tickEnd = edge + d * outLen;
            for (float tick : yTicks) {
                float py = rect.y + rect.height -
                           axes.dataToFraction({0.0f, tick}).y * rect.height;
                if (py < rect.y || py > rect.y + rect.height) { ++i; continue; }
                auto label = tickLabel(tc, *fmt, tick, i++);
                if (label.empty() || tc.hiddenLabels.contains(i - 1))
                    continue;
                auto m = measureRichText(label, yTickScale);
                float rot = style.yAxis.tickFont.rotation;
                tickLabelW = std::max(tickLabelW,
                    m.width * std::abs(std::cos(rot)) +
                    m.height * std::abs(std::sin(rot)));
                // Label sits outside the tick mark, vertically centered.
                float baseY = py + m.ascent - m.height * 0.5f;
                float x, y = baseY;
                if (rot != 0.0f) {
                    // Rotated: anchor the baseline's edge at the tick mark,
                    // keeping the same vertical center (mpl va='center').
                    float cosR = std::cos(rot), sinR = std::sin(rot);
                    float e = right ? tickEnd + kTickSpacing
                                    : tickEnd - kTickSpacing;
                    x = right ? e : e - m.width * cosR;
                    y = right ? baseY : baseY - m.width * sinR;
                } else {
                    x = right ? tickEnd + kTickSpacing
                              : tickEnd - kTickSpacing - m.width;
                }
                drawRichText(cmd, fullRect, label, x, y, yTickColor, yTickScale,
                             rot, plot::HAlign::Left,
                             &style.yAxis.tickFont);
            }
            if (logMinorLabels(tc, axes.yscale())) {
                auto minor = axisMinorTicks(tc, axes.yscale(), vp.y.min,
                                            vp.y.max, yTicks);
                plot::LogFormatterSciNotation defMinorFmt;
                plot::Formatter* mfmt = tc.minorFormatter
                                            ? tc.minorFormatter.get()
                                            : &defMinorFmt;
                mfmt->setViewInterval(vp.y.min, vp.y.max);
                mfmt->setLocs(minor);
                float mScale = yTickScale;
                float mEnd = edge + d * tc.minorSize * figDpi / 72.0f *
                                (1.0f - tickInFrac(tc));
                float mGap = tc.minorPad * figDpi / 72.0f;
                int mi = 0;
                for (float t : minor) {
                    float py = rect.y + rect.height -
                               axes.dataToFraction({0.0f, t}).y * rect.height;
                    if (py < rect.y || py > rect.y + rect.height) { ++mi; continue; }
                    auto label = fmtLabel(*mfmt, t, mi++);
                    if (label.empty() ||
                        tc.hiddenMinorLabels.contains(mi - 1))
                        continue;
                    auto m = measureRichText(label, mScale);
                    float x = right ? mEnd + mGap
                                    : mEnd - mGap - m.width;
                    float y = py + m.ascent - m.height * 0.5f;
                    drawRichText(cmd, fullRect, label, x, y, yTickColor,
                                 mScale, 0.0f, plot::HAlign::Left,
                                 &style.yAxis.tickFont);
                }
            }
        };
        if (!labelsHidden) {
            if (yf.labelsNear) drawLabelsSide(false);
            if (yf.labelsFar) drawLabelsSide(true);
        }
        // Offset text above the top of the y axis, on the label side
        // (mpl: offsetText x follows the tick side).
        const bool offRight = yf.labelsFar && !yf.labelsNear;
        auto off = fixMinus(fmt->offsetText());
        if (!labelsHidden && !off.empty()) {
            auto m = measureRichText(off, yTickScale * 0.8f);
            float x = offRight ? rect.x + rect.width - m.width : rect.x;
            float y = rect.y - kTickSpacing;
            drawRichText(cmd, fullRect, off, x, y, yTickColor, yTickScale * 0.8f,
                         0.0f, plot::HAlign::Left, &style.yAxis.tickFont);
        }

        // --- Y axis label --- Rotate -90° (clockwise in screen space,
        // Y-down) so the label reads bottom-to-top. The rotation origin is
        // the text baseline (x, y). After rotation:
        //   - text width becomes vertical extent (upward from origin)
        //   - ascent becomes leftward extent, descent becomes rightward
        // We want: vertical center at axes middle, positioned outside the
        // tick labels using their measured width.
        const bool labelRight = yf.labelFar;
        if (!style.yAxis.label.empty()) {
            auto m = measureRichText(style.yAxis.label, yLabelScale);
            // Y position: y - width/2 = axes vertical center
            float oy = rect.y + rect.height / 2.0f + m.width / 2.0f;
            float edge = edgeFor(labelRight);
            float d = labelRight ? 1.0f : -1.0f;
            // mpl: the label clears the tick labels on its own side.
            float sideLabelW = (labelRight ? yf.labelsFar : yf.labelsNear)
                                   ? tickLabelW : 0.0f;
            // Center of rotated text = x + m.height/2 - m.ascent, so
            // x = centerPos - m.height/2 + m.ascent.
            float centerPos = edge + d * (outLen + kTickSpacing +
                                          sideLabelW +
                                          style.yAxis.labelPad * figDpi / 72.0f +
                                          m.height * 0.5f);
            float ox = centerPos - m.height / 2.0f + m.ascent +
                       d * axes.yLabelShiftPx;
            constexpr float kRotMinus90 = -1.5707963267948966f; // -π/2
            drawRichText(cmd, fullRect,
                style.yAxis.label, ox, oy, style.yAxis.labelColor, yLabelScale,
                kRotMinus90 + style.yAxis.labelFont.rotation,
                plot::HAlign::Center, &style.yAxis.labelFont);
        }

        // --- Secondary y axis (matplotlib secondary_yaxis): right-side
        // tick labels in transformed units positioned via the inverse map.
        if (auto sec = axes.secondaryY()) {
            float slo = sec->forward(vp.y.min), shi = sec->forward(vp.y.max);
            auto sTicks = axisTicks(tc, plot::AxisScale{}, slo, shi,
                                    float(rect.height), style.yAxis.tickFont.size,
                                    figDpi, true);
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
                auto m = measureRichText(label, yTickScale);
                drawRichText(cmd, fullRect, label,
                             rEdge + outLen + kTickSpacing,
                             py + m.ascent - m.height * 0.5f,
                             yTickColor, yTickScale, 0.0f,
                             plot::HAlign::Left, &style.yAxis.tickFont);
            }
        }
    }
}

Renderer::AxisLabelDepths Renderer::measureAxisLabelDepths(
        const plot::Axes& axes, plot::Rect2D rect) {
    AxisLabelDepths d{};
    if (!textReady_) return d;
    const auto& style = axes.style();
    const float figDpi =
        axes.figure() ? axes.figure()->dpi() : style.dpi;
    const float xTickScale =
        style.xAxis.tickFont.size * figDpi / (72.0f * 16.0f);
    const float yTickScale =
        style.yAxis.tickFont.size * figDpi / (72.0f * 16.0f);
    const float xLabelScale =
        style.xAxis.labelFont.size * figDpi / (72.0f * 16.0f);
    const float yLabelScale =
        style.yAxis.labelFont.size * figDpi / (72.0f * 16.0f);
    const auto& vp = axes.viewport();
    bool has3D = std::ranges::any_of(axes.drawOrder(),
        [](const plot::IPlot* pl) { return pl->is3D(); });

    if (!has3D && style.xAxis.visible && !style.xAxis.label.empty()) {
        const auto& tc = style.xAxis.ticks;
        auto xTicks = axisTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                float(rect.width), style.xAxis.tickFont.size,
                                figDpi, false);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, xTicks, style, axes.xscale(),
                                             vp.x.min, vp.x.max,
                                             defaultFmt, strFmt, logFmt);
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        float tickLabelH = 0.0f;
        int i = 0;
        for (float tick : xTicks) {
            float px = rect.x + axes.dataToFraction({tick, 0.0f}).x *
                       rect.width;
            if (px < rect.x || px > rect.x + rect.width) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, xTickScale);
            float rot = style.xAxis.tickFont.rotation;
            tickLabelH = std::max(tickLabelH,
                m.ascent + m.width * std::abs(std::sin(rot)));
        }
        auto m = measureRichText(style.xAxis.label, xLabelScale);
        d.x = outLen + tc.majorPad * figDpi / 72.0f + tickLabelH +
              style.xAxis.labelPad * figDpi / 72.0f + m.height;
    }

    if (!has3D && style.yAxis.visible && !style.yAxis.label.empty()) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                float(rect.height), style.yAxis.tickFont.size,
                                figDpi, true);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::LogFormatterMathtext logFmt;
        plot::Formatter* fmt = axisFormatter(tc, yTicks, style, axes.yscale(),
                                             vp.y.min, vp.y.max,
                                             defaultFmt, strFmt, logFmt);
        float outLen = tc.majorSize * figDpi / 72.0f * (1.0f - tickInFrac(tc));
        float tickLabelW = 0.0f;
        int i = 0;
        for (float tick : yTicks) {
            float py = rect.y + rect.height -
                       axes.dataToFraction({0.0f, tick}).y * rect.height;
            if (py < rect.y || py > rect.y + rect.height) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, yTickScale);
            float rot = style.yAxis.tickFont.rotation;
            tickLabelW = std::max(tickLabelW,
                m.width * std::abs(std::cos(rot)) +
                m.height * std::abs(std::sin(rot)));
        }
        auto m = measureRichText(style.yAxis.label, yLabelScale);
        d.y = outLen + tc.majorPad * figDpi / 72.0f + tickLabelW +
              style.yAxis.labelPad * figDpi / 72.0f + m.height;
    }
    return d;
}

void Renderer::alignAxesLabels(plot::Figure& figure) {
    struct Entry { plot::Axes* ax; float depth; uint32_t key; };
    std::vector<Entry> xs, ys;
    for (auto& p : figure.placements()) {
        plot::Axes* ax = p.axes.get();
        ax->xLabelShiftPx = ax->yLabelShiftPx = 0.0f;
        if (p.mode != plot::PlacementMode::Grid) continue;
        auto d = measureAxisLabelDepths(*ax, ax->rect);
        // mpl grouping: bottom xlabels share rowspan.stop, top labels
        // rowspan.start; left ylabels share colspan.start, right labels
        // colspan.stop.
        if (figure.alignXLabels() && d.x > 0.0f)
            xs.push_back({ax, d.x, ax->xTicksTop()
                                   ? p.spec.row
                                   : p.spec.row + p.spec.rowSpan});
        if (figure.alignYLabels() && d.y > 0.0f)
            ys.push_back({ax, d.y, ax->yTicksRight()
                                   ? p.spec.col + p.spec.colSpan
                                   : p.spec.col});
    }
    auto groupMax = [](const std::vector<Entry>& v, uint32_t key) {
        float m = 0.0f;
        for (const auto& e : v) if (e.key == key) m = std::max(m, e.depth);
        return m;
    };
    for (auto& e : xs) e.ax->xLabelShiftPx = groupMax(xs, e.key) - e.depth;
    for (auto& e : ys) e.ax->yLabelShiftPx = groupMax(ys, e.key) - e.depth;
}

void Renderer::drawSpines(vk::CommandBuffer cmd, const plot::Axes& axes,
                           plot::Rect2D rect) {
    if (!spineInited_) return;
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    // mpl axison=False: the whole axis furniture is dropped.
    if (!axes.axison()) return;
    if (!style.xAxis.visible && !style.yAxis.visible) return;

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Draw the border as pixel-aligned filled quads (matplotlib draws ~1px
    // spines). Filled quads aligned to integer pixel coordinates get 100%
    // coverage → pure black even under MSAA, unlike a stroked line centered
    // on the boundary which spreads across two pixels.
    // mpl frame_on=False: the spine frame is removed while tick marks
    // and labels keep drawing.
    auto spineColor = style.xAxis.color;
    float t = std::max(style.xAxis.lineWidth, 1.0f);
    float x0 = float(rect.x), y0 = float(rect.y);
    float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
    const auto& sp = axes.spines();
    auto quad = [&](float qx0, float qy0, float qx1, float qy1,
                    plot::Color c) {
        spineRenderer_.drawFilledRect(cmd, fullRect, ext,
            plot::Rect2D{int32_t(std::lround(qx0)),
                         int32_t(std::lround(qy0)),
                         uint32_t(std::lround(qx1 - qx0)),
                         uint32_t(std::lround(qy1 - qy0))}, c);
    };
    if (axes.frameOn()) {
        // mpl Spine.set_position: a repositioned spine draws as a quad
        // centered on its resolved pixel position; a default spine keeps
        // its pixel-aligned inside-edge quad.
        // `edge` is the outer canvas coordinate of the spine's axes-edge
        // (y1 for bottom, y0 for top, x0 for left, x1 for right); `dir`
        // is +1 when the spine extends inward in +pixel direction
        // (top, left) and -1 otherwise (bottom, right).
        auto drawSpine = [&](std::string_view side, bool horiz,
                             float edge, float dir) {
            const auto& s = sp.side(side);
            if (!s.visible) return;
            auto g = axes.spineLine(side, rect);
            plot::Color c = s.color.value_or(spineColor);
            float sw = std::max(s.lineWidth.value_or(t), 1.0f);
            float p = s.positionSet ? g.pos : edge;
            float from = s.positionSet || s.bounds ? g.from
                       : (horiz ? x0 : y0);
            float to = s.positionSet || s.bounds ? g.to
                     : (horiz ? x1 : y1);
            // mpl set_linestyle/set_dashes: a dashed spine is stroked
            // rather than drawn as a solid quad.
            std::vector<float> dashes = s.dashes;
            if (dashes.empty() && s.lineStyle &&
                *s.lineStyle != plot::LineStyle::Solid)
                dashes = plot::dashPattern(*s.lineStyle, sw);
            if (!dashes.empty()) {
                plot::StrokeParams sp2;
                sp2.width = sw;
                sp2.dashes = std::move(dashes);
                plot::Point2D pts[2] = {horiz ? plot::Point2D{from, p}
                                              : plot::Point2D{p, from},
                                        horiz ? plot::Point2D{to, p}
                                              : plot::Point2D{p, to}};
                auto mesh = plot::strokePolyline(pts, sp2);
                spineRenderer_.drawTriangles(cmd, fullRect, ext,
                                             mesh.verts, c);
            } else if (s.positionSet) {
                if (horiz) quad(from, p - sw * 0.5f, to, p + sw * 0.5f, c);
                else       quad(p - sw * 0.5f, from, p + sw * 0.5f, to, c);
            } else if (horiz) {
                quad(from, std::min(edge, edge + dir * sw),
                     to, std::max(edge, edge + dir * sw), c);
            } else {
                quad(std::min(edge, edge + dir * sw), from,
                     std::max(edge, edge + dir * sw), to, c);
            }
        };
        drawSpine("bottom", true,  y1, -1.0f);
        drawSpine("top",    true,  y0, +1.0f);
        drawSpine("left",   false, x0, +1.0f);
        drawSpine("right",  false, x1, -1.0f);
    }

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
                                as.tickFont.size, figDpi, yAxis);
        auto majorFrac = toFrac(majors, yAxis);
        // mpl Spine.set_position: tick marks ride on the repositioned
        // spine — build a rect whose tick-side edge sits at the spine's
        // pixel position.
        plot::Rect2D tickRect = rect;
        const auto& spec =
            sp.side(yAxis ? (farSide ? "right" : "left")
                          : (farSide ? "top" : "bottom"));
        if (spec.positionSet) {
            float pos = axes.spineLine(yAxis ? (farSide ? "right" : "left")
                                             : (farSide ? "top" : "bottom"),
                                       rect).pos;
            if (yAxis) { tickRect.x = int32_t(pos); tickRect.width = 0; }
            else       { tickRect.y = int32_t(pos); tickRect.height = 0; }
        }
        spineRenderer_.drawTicks(cmd, fullRect, ext, tickRect, majorFrac,
                                 as.color, tc.majorSize * figDpi / 72.0f,
                                 yAxis, 0.0f, 1.0f, inF, farSide,
                                 std::max(tc.majorWidth * figDpi / 72.0f, 1.5f));
        auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
        if (!minors.empty()) {
            auto minorFrac = toFrac(minors, yAxis);
            spineRenderer_.drawTicks(cmd, fullRect, ext, tickRect, minorFrac,
                                     as.color, tc.minorSize * figDpi / 72.0f,
                                     yAxis, 0.0f, 1.0f, inF, farSide,
                                     std::max(tc.minorWidth * figDpi / 72.0f, 1.0f));
        }
    };
    // mpl label_outer(remove_inner_ticks=True): inner axes drop their
    // tick marks too (by default only the labels are hidden).
    const bool xMarksHidden = axes.innerTicksRemoved() &&
                              axes.xTickLabelsHidden();
    const bool yMarksHidden = axes.innerTicksRemoved() &&
                              axes.yTickLabelsHidden();
    if (style.xAxis.visible && !xMarksHidden) {
        // mpl set_ticks_position/tick_params: marks on the enabled
        // sides — 'both' draws near+far, 'none' draws neither.
        const auto& xf = axes.xFurniture();
        if (xf.marksFar)
            drawAxisTicks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                          false, true);
        if (xf.marksNear)
            drawAxisTicks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                          false, false);
    }
    if (style.yAxis.visible && !yMarksHidden) {
        const auto& yf = axes.yFurniture();
        if (yf.marksFar)
            drawAxisTicks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                          true, true);
        if (yf.marksNear)
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
    // mpl axison=False removes the axis objects including their grid.
    if (!axes.axison()) return;
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    const auto& vp = axes.viewport();
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};

    const float x0 = float(rect.x), y0 = float(rect.y);
    const float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);

    auto drawSet = [&](bool yAxis, std::span<const float> fracs,
                       plot::Color color, float widthPt,
                       const std::string& lineStyle) {
        if (fracs.empty()) return;
        const float w = std::max(widthPt * figDpi / 72.0f, 1.0f);
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
                                as.tickFont.size, figDpi, yAxis);
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

/// Draw a mpl FancyBboxPatch-style text background box: a boxStyle
/// outline when set (pad/rounding in units of `mutationPx` — the text
/// fontsize in px), else a plain rect. Face drawn when face.a > 0,
/// edge stroked at `edgeW` px when edge.a > 0.
inline void drawTextBbox(vk::CommandBuffer cmd, vk::Rect2D clip,
                         vk::Extent2D res, primitives::SpineRenderer& spine,
                         float x0, float y0, float w, float h, float padPx,
                         plot::Color face, plot::Color edge, float edgeW,
                         const std::optional<plot::BoxStyleSpec>& boxStyle,
                         float mutationPx) {
    if (boxStyle) {
        // boxStylePath expands the rect by pad*mutationSize itself —
        // pass the raw text rect, not a pre-padded one.
        auto bs = *boxStyle;
        bs.mutationSize *= mutationPx;
        auto path = plot::boxStylePath(x0, y0, w, h, bs);
        for (auto& sp : path.toPolylines(24)) {
            if (face.a > 0.0f) {
                auto tris = plot::earClip(sp.points);
                if (!tris.empty())
                    spine.drawTriangles(cmd, clip, res,
                                        std::span{tris}, face);
            }
            if (edge.a > 0.0f) {
                auto ring = sp.points;
                if (sp.closed && !ring.empty())
                    ring.push_back(ring.front());
                spine.drawLineStrip(cmd, clip, res,
                                    std::span<const plot::Point2D>{ring},
                                    edge, edgeW);
            }
        }
    } else {
        if (face.a > 0.0f)
            spine.drawFilledRect(cmd, clip, res,
                {int32_t(x0 - padPx), int32_t(y0 - padPx),
                 uint32_t(std::max(w + 2 * padPx, 0.0f)),
                 uint32_t(std::max(h + 2 * padPx, 0.0f))},
                face);
        if (edge.a > 0.0f)
            spine.drawRect(cmd, clip, res,
                {int32_t(x0 - padPx), int32_t(y0 - padPx),
                 uint32_t(std::max(w + 2 * padPx, 0.0f)),
                 uint32_t(std::max(h + 2 * padPx, 0.0f))},
                edge, edgeW);
    }
}

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
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
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
                         style.xAxis.gridLineWidth * figDpi / 72.0f);
        }
    }
    // Concentric circles at r ticks (y-axis grid), or set_rgrids radii.
    if (style.yAxis.grid) {
        auto rTicks = !axes.rgrids().empty()
            ? axes.rgrids()
            : axisTicks(style.yAxis.ticks, axes.yscale(),
                        vp.y.min, vp.y.max, float(rect.height),
                        style.yAxis.tickFont.size, figDpi, true);
        for (float r : rTicks) {
            if (r <= 0.0f || r > rmax) continue;
            auto circ = polarCircle(axes, rect, r);
            strokePxPoly(cmd, spineRenderer_, clip, ext, circ,
                         style.yAxis.gridColor,
                         style.yAxis.gridLineWidth * figDpi / 72.0f);
        }
    }
}

void Renderer::drawPolarSpineAndLabels(vk::CommandBuffer cmd,
                                       const plot::Axes& axes,
                                       plot::Rect2D rect) {
    const auto& style = axes.style();
    const auto& vp = axes.viewport();
    const float figDpi =
        axes.figure() ? axes.figure()->dpi() : style.dpi;
    float rmax = std::max(std::fabs(vp.y.min), std::fabs(vp.y.max));
    if (rmax <= 0.0f) rmax = 1.0f;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    constexpr float kPi = 3.14159265358979323846f;

    // Circular outer spine at rmax.
    auto circ = polarCircle(axes, rect, rmax);
    strokePxPoly(cmd, spineRenderer_, fullRect, ext, circ,
                 style.xAxis.color,
                 std::max(style.xAxis.lineWidth * figDpi / 72.0f, 1.0f));

    if (!textReady_) return;
    float scale = style.xAxis.tickFont.size * figDpi / (72.0f * 16.0f);
    auto labelColor =
        style.xAxis.ticks.labelColor.value_or(style.xAxis.color);
    const auto& tc = style.xAxis.ticks;
    float pad = tc.majorPad * figDpi / 72.0f + tc.majorSize * figDpi / 72.0f;

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
                     labelColor, scale, 0.0f, plot::HAlign::Center,
                     &style.xAxis.tickFont);
    }

    // r labels along the 22.5° radial (mpl rlabel_position).
    if (style.yAxis.visible) {
        const auto& ytc = style.yAxis.ticks;
        auto rTicks = !axes.rgrids().empty()
            ? axes.rgrids()
            : axisTicks(ytc, axes.yscale(), vp.y.min, vp.y.max,
                        float(rect.height), style.yAxis.tickFont.size,
                        figDpi, true);
        plot::ScalarFormatter fmt;
        fmt.setLocs(rTicks);
        float labelAng = axes.rlabelPosition() * kPi / 180.0f;
        const float rScale = style.yAxis.tickFont.size * figDpi /
                             (72.0f * 16.0f);
        int i = 0;
        for (float r : rTicks) {
            if (r <= 0.0f || r > rmax) { ++i; continue; }
            auto label = fmt.format(r, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, rScale * 0.8f);
            auto p = polarToPx(axes, rect, labelAng, r);
            drawRichText(cmd, fullRect, label, p.x - m.width * 0.5f,
                         p.y + m.ascent - m.height * 0.5f,
                         labelColor, rScale * 0.8f,
                         0.0f, plot::HAlign::Center,
                         &style.yAxis.tickFont);
        }
    }
}

/// Collect legend entries (label + color + marker) from an axes' plot
/// layers; a handler_map entry overrides the plot's own handle.
/// mpl handles= replaces collection; labels= overrides labels
/// positionally; legend.reverse flips the order.
std::vector<Renderer::LegendEntry>
Renderer::collectLegendEntries(const plot::Axes& axes) {
    const auto& lg = axes.style().legend;
    std::vector<LegendEntry> entries;
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

namespace {

/// mpl loc name/code → (anchor fraction, box-fraction corner).
struct LocAnchor { float fx, fy, bx, by; };
LocAnchor parseLegendLoc(std::string_view loc) {
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
}

} // namespace

/// Measure a legend box (mpl packing rules) — shared by drawLegend and
/// drawFigureLegend.
Renderer::LegendLayout
Renderer::measureLegend(const std::vector<LegendEntry>& entries,
                        const plot::LegendStyle& lg, float dpi) {
    LegendLayout L;
    // Font scale: same points→pixels mapping as the rest of the text
    // pipeline (16px atlas reference at 72dpi).
    L.scale = lg.font.size * dpi / (72.0f * 16.0f);
    L.fontPx = 16.0f * L.scale;
    L.pad = lg.borderPad * L.fontPx + (lg.fancyBox ? 2.0f : 0.0f);
    L.handleW = lg.handleLength * L.fontPx;
    L.textGap = lg.handleTextPad * L.fontPx;
    L.colGap = lg.columnSpacing * L.fontPx;
    L.rowSep = lg.labelSpacing * L.fontPx;

    // mpl handle box: `height = handleheight*fontsize - descent` tall
    // above the text baseline, `descent` below it, where
    // descent = 0.35*fontsize*(handleheight - 0.7) (legend.py heuristic).
    L.hBelow = 0.35f * L.fontPx * (lg.handleHeight - 0.7f);
    L.hBoxH = lg.handleHeight * L.fontPx - L.hBelow;
    L.hAbove = L.hBoxH - L.hBelow;

    // Column-major split into columns (matplotlib: each column is a
    // contiguous run of `rows` entries).
    const int n = static_cast<int>(entries.size());
    L.rows = lg.nrows > 0 ? lg.nrows
                          : (n + std::max(1, lg.ncols) - 1) /
                                std::max(1, lg.ncols);
    L.cols = (n + L.rows - 1) / L.rows;

    // Measure each label once (mpl TextArea extents drive packing).
    L.itemW.resize(n);
    L.itemAbove.resize(n);
    L.itemBelow.resize(n);
    for (int i = 0; i < n; ++i) {
        auto m = measureRichText(entries[i].label, L.scale);
        L.itemW[i] = m.width;
        L.itemAbove[i] = std::max(m.ascent, L.hAbove);
        L.itemBelow[i] = std::max(m.height - m.ascent, L.hBelow);
    }

    // Per-column width = handle + text gap + widest label in the column.
    // Per-column height = sum of item extents + labelspacing seps (each
    // column is its own VPacker in mpl — rows need not align).
    L.colW.assign(L.cols, 0.0f);
    std::vector<float> colH(L.cols, 0.0f);
    for (int c = 0; c < L.cols; ++c) {
        float maxW = 0, h = 0;
        for (int r = 0; r < L.rows; ++r) {
            int idx = c * L.rows + r;
            if (idx >= n) break;
            maxW = std::max(maxW, L.itemW[idx]);
            h += L.itemAbove[idx] + L.itemBelow[idx] + (r ? L.rowSep : 0.0f);
        }
        L.colW[c] = L.handleW + L.textGap + maxW;
        colH[c] = h;
    }
    float contentW = 0;
    for (float w : L.colW) contentW += w;
    contentW += L.colGap * (L.cols - 1);
    const float contentH = *std::ranges::max_element(colH);

    // Title row (mpl packs the title TextArea + labelspacing sep above
    // the handle box; the sep applies even when the title is empty).
    float titleW = 0.0f;
    L.titleScale = lg.titleFont.size * dpi / (72.0f * 16.0f);
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, L.titleScale);
        titleW = m.width;
        L.titleH = m.height;
    }

    L.boxW = L.pad * 2 + std::max(contentW, titleW);
    L.boxH = L.pad * 2 + L.titleH + L.rowSep + contentH;

    // mpl HPacker align="baseline": the first items' baselines align
    // across columns, so the tallest first item sets the shared line.
    for (int c = 0; c < L.cols; ++c)
        L.firstAbove = std::max(L.firstAbove, L.itemAbove[c * L.rows]);
    return L;
}

/// Paint a measured legend box whose (bx,by) box-fraction corner sits at
/// `anchor` (canvas px). Returns the box rect.
plot::Rect2D Renderer::paintLegendBox(
    vk::CommandBuffer cmd, const std::vector<LegendEntry>& entries,
    const plot::LegendStyle& lg, const LegendLayout& L,
    plot::Color textColor, plot::Point2D anchor, float bx, float by,
    float forceW) {
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    const int n = static_cast<int>(entries.size());
    const float fontPx = L.fontPx;
    const float handleW = L.handleW, textGap = L.textGap;
    const float rowSep = L.rowSep;
    const float pad = L.pad, scale = L.scale;

    // mpl mode="expand" / 4-tuple bbox_to_anchor: widen the box; each
    // column expands by an equal share (mpl HPacker expand semantics).
    float boxW = L.boxW;
    float extraW = 0.0f;
    if (forceW > boxW) { extraW = forceW - boxW; boxW = forceW; }
    const float colGap = L.colGap;
    std::vector<float> colW = L.colW;
    for (auto& w : colW) w += extraW / std::max(1, L.cols);

    const float boxX = anchor.x - bx * boxW;
    const float boxY = anchor.y - (1.0f - by) * L.boxH;
    auto pxRect = [](float x, float y, float w, float h) {
        return plot::Rect2D{int32_t(std::lround(x)),
                            int32_t(std::lround(y)),
                            uint32_t(std::lround(w)),
                            uint32_t(std::lround(h))};
    };
    const plot::Rect2D boxRect = pxRect(boxX, boxY, boxW, L.boxH);

    // Drop shadow behind the box.
    if (lg.shadow) {
        const float so = fontPx * 0.25f;
        spineRenderer_.drawFilledRect(cmd, fullRect, ext,
            pxRect(boxX + so, boxY + so, boxW, L.boxH),
            plot::Color::fromRgba8(0, 0, 0, 100));
    }

    if (lg.frameOn) {
        // Semi-transparent background.
        auto bg = lg.faceColor;
        bg.a *= lg.frameAlpha;
        spineRenderer_.drawFilledRect(cmd, fullRect, ext, boxRect, bg);
        spineRenderer_.drawRect(cmd, fullRect, ext, boxRect, lg.edgeColor, 1.0f);
    }

    // mpl alignment: the entry block + title align left/center/right
    // within the box's inner width.
    const float innerW = boxW - 2.0f * pad;
    const float alignF = lg.alignment == "left" ? 0.0f
                       : lg.alignment == "right" ? 1.0f : 0.5f;
    float contentW = 0.0f;
    for (float w : colW) contentW += w;
    contentW += colGap * (L.cols - 1);
    const float blockShift = std::max(0.0f, innerW - contentW) * alignF;

    // Title row; the handle box always follows a labelspacing sep below
    // the title area (mpl VPacker sep).
    float contentTop = boxY + pad;
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, L.titleScale);
        drawRichText(cmd, fullRect, lg.title,
                     boxX + pad + std::max(0.0f, innerW - m.width) * alignF,
                     contentTop + m.ascent, textColor, L.titleScale,
                     0.0f, plot::HAlign::Left, &lg.titleFont);
    }
    contentTop += L.titleH + rowSep;

    const auto labelColor = lg.labelColor.value_or(textColor);
    const float markerSize = fontPx * lg.markerScale;

    // Markers drawn on the handle (mpl numpoints/scatterpoints).
    auto handlePoints = [&](const LegendEntry& e) {
        if (e.points >= 0) return e.points;
        if (e.points == -2) return lg.numpoints;
        return e.marker == plot::LegendMarker::Circle ? lg.scatterpoints : 0;
    };
    auto disc = [&](float cx, float cy, float r, plot::Color col) {
        std::vector<plot::Point2D> fan;
        fan.reserve(3 * 8);
        for (int k = 0; k < 8; ++k) {
            const float a0 = k * 0.78539816f, a1 = (k + 1) * 0.78539816f;
            fan.push_back({cx, cy});
            fan.push_back({cx + r * std::cos(a0), cy + r * std::sin(a0)});
            fan.push_back({cx + r * std::cos(a1), cy + r * std::sin(a1)});
        }
        spineRenderer_.drawTriangles(cmd, fullRect, ext, fan, col);
    };

    // Draw each entry: handle (line/marker) + text label. Each column
    // packs its own items top-down with labelspacing between them.
    for (int c = 0; c < L.cols; ++c) {
        float colX = boxX + pad + blockShift;
        for (int j = 0; j < c; ++j) colX += colW[j] + colGap;
        float baseline = contentTop + L.firstAbove;
        int prev = -1;
        for (int r = 0; r < L.rows; ++r) {
            const int i = c * L.rows + r;
            if (i >= n) break;
            if (prev >= 0)
                baseline += L.itemBelow[prev] + rowSep + L.itemAbove[i];
            prev = i;
            // Handle box: hAbove above the baseline, hBelow below.
            const float midY = baseline - L.hAbove + L.hBoxH * 0.5f;
            const auto& e = entries[i];
            const int npts = handlePoints(e);

            // mpl markerfirst: [handle, text]; else [text, handle].
            const float hX = lg.markerFirst
                ? colX
                : colX + L.itemW[i] + textGap;
            const float tX = lg.markerFirst
                ? colX + handleW + textGap
                : colX;

            if (e.marker == plot::LegendMarker::Line) {
                plot::Point2D pts[] = {{hX, midY}, {hX + handleW, midY}};
                spineRenderer_.drawLineStrip(cmd, fullRect, ext, pts, e.color, 2.0f);
                // mpl numpoints: markers evenly spaced on the segment.
                const float rad = markerSize * 0.3f;
                for (int p = 0; p < npts; ++p) {
                    const float fx = npts == 1 ? 0.5f
                        : float(p) / float(npts - 1);
                    disc(hX + fx * handleW, midY, rad, e.color);
                }
            } else if (e.marker == plot::LegendMarker::Circle) {
                // mpl scatterpoints: discs spread across the handle.
                const float rad = markerSize * (npts > 1 ? 0.35f : 0.5f);
                for (int p = 0; p < npts; ++p) {
                    const float fx = npts == 1 ? 0.5f
                        : float(p) / float(npts - 1);
                    disc(hX + handleW * 0.15f + fx * handleW * 0.7f,
                         midY, rad, e.color);
                }
            } else {
                // Filled square centered in the handle area.
                const float sx = hX + (handleW - markerSize) / 2.0f;
                spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                    {int32_t(sx), int32_t(midY - markerSize / 2.0f),
                     uint32_t(markerSize), uint32_t(markerSize)},
                    e.color);
            }

            // Label text: baseline at the item's shared baseline.
            drawRichText(cmd, fullRect, e.label, tX, baseline,
                         labelColor, scale, 0.0f, plot::HAlign::Left,
                         &lg.font);
        }
    }
    return boxRect;
}

void Renderer::drawFigureLegend(vk::CommandBuffer cmd,
                                const plot::Figure& fig) {
    const auto& lg = fig.figureLegend();
    if (!spineInited_ || !textReady_ || !lg.visible) return;

    std::vector<LegendEntry> entries;
    for (auto& p : fig.placements()) {
        if (!p.axes->visible()) continue;
        auto es = collectLegendEntries(*p.axes);
        entries.insert(entries.end(),
                       std::make_move_iterator(es.begin()),
                       std::make_move_iterator(es.end()));
    }
    if (entries.empty()) return;

    auto L = measureLegend(entries, lg, fig.style().dpi);
    auto ext = backend_.extent();

    // Figure-space anchor: loc resolves against the canvas edge, flush
    // to the corner like mpl's Figure.legend (no borderaxespad inset).
    LocAnchor la = parseLegendLoc(lg.location);
    const bool hasAnchor = lg.anchorX >= 0.0f || lg.anchorY >= 0.0f;
    const float afx = hasAnchor ? lg.anchorX : la.fx;
    const float afy = hasAnchor ? lg.anchorY : la.fy;
    const bool sub = hasAnchor && lg.anchorW >= 0.0f && lg.anchorH >= 0.0f;
    plot::Point2D anchor = sub
        ? plot::Point2D{(afx + la.fx * lg.anchorW) * float(ext.width),
                        (1.0f - afy - la.fy * lg.anchorH) *
                            float(ext.height)}
        : plot::Point2D{afx * float(ext.width),
                        (1.0f - afy) * float(ext.height)};
    anchor.x += lg.dragOffset.x;
    anchor.y += lg.dragOffset.y;

    const float forceW = lg.expand
        ? (sub ? lg.anchorW * float(ext.width) : float(ext.width))
        : -1.0f;
    paintLegendBox(cmd, entries, lg, L, fig.style().textColor,
                   anchor, la.bx, la.by, forceW);
}

void Renderer::drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                          plot::Rect2D rect) {
    if (!spineInited_ || !textReady_) return;
    const auto& style = axes.style();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    const auto& lg = style.legend;
    if (!lg.visible) { axes.setLegendBox({}); return; }

    auto entries = collectLegendEntries(axes);
    if (entries.empty()) { axes.setLegendBox({}); return; }

    auto ext = backend_.extent();

    const float scale = lg.font.size * figDpi / (72.0f * 16.0f);
    const float fontPx = 16.0f * scale;
    auto L = measureLegend(entries, lg, figDpi);
    const float boxW = L.boxW, boxH = L.boxH;

    LocAnchor la = parseLegendLoc(lg.location);
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
    // else the loc's axes fraction. A 4-tuple anchor (anchorW/H >= 0)
    // defines a sub-box the loc point resolves inside (mpl).
    const bool hasAnchor = lg.anchorX >= 0.0f || lg.anchorY >= 0.0f;
    float afx = hasAnchor ? lg.anchorX : la.fx;
    float afy = hasAnchor ? lg.anchorY : la.fy;
    const bool sub = hasAnchor && lg.anchorW >= 0.0f && lg.anchorH >= 0.0f;
    float px, py, subW = -1.0f;
    if (hasAnchor && lg.anchorSpace == plot::CoordSystem::Figure) {
        if (sub) {
            px = (afx + la.fx * lg.anchorW) * ext.width;
            py = (1.0f - afy - la.fy * lg.anchorH) * ext.height;
            subW = lg.anchorW * ext.width;
        } else {
            px = afx * ext.width;
            py = (1.0f - afy) * ext.height;
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

    // mpl mode="expand": fill the anchor width (bbox width, else the
    // axes width minus borderaxespad margins).
    float forceW = -1.0f;
    if (lg.expand)
        forceW = subW >= 0.0f
            ? subW
            : float(rect.width) - 2.0f * lg.borderAxesPad * fontPx;

    const auto boxRect = paintLegendBox(cmd, entries, lg, L,
                                        style.textColor, {px, py},
                                        la.bx, la.by, forceW);
    axes.setLegendBox(boxRect);
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
    figure.setStale(false);
}

bool Renderer::renderIfStale(plot::Figure& figure) {
    if (!frameValid_ || figure.stale()) {
        prepare(figure);
        renderFrame(figure);   // clears fig.stale
        frameValid_ = true;
        return true;
    }
    return false;
}

bool Renderer::blitCaptureBackground(plot::Figure& figure) {
    renderFrameSubset(figure, DrawSubset::StaticOnly);
    return backend_.blitCapture();
}

void Renderer::blitDrawAnimated(plot::Figure& figure) {
    renderFrameSubset(figure, DrawSubset::AnimatedOnly);
}

void Renderer::renderFrameSubset(plot::Figure& figure, DrawSubset subset) {
    ++frameSeq_;
    auto ext = backend_.extent();
    figure.layout(plot::Extent2D{ext.width, ext.height});
    mathFontset_ = text::parseMathFontset(figure.style().mathFontset);
    if (figure.alignXLabels() || figure.alignYLabels())
        alignAxesLabels(figure);

    // GPU pre-pass: IPlot::preDraw records compute work (line
    // tessellation, ...) that must run outside the render pass. The
    // pre-pass is submitted on the graphics queue ahead of the frame —
    // same-queue ordering makes its writes visible to the draw
    // submission (same mechanism as staging uploads).
    {
        auto& ctx = backend_.context();
        if (!preCmd_)
            preCmd_.emplace(ctx.device.handle(), ctx.graphicsPool.handle());
        preCmd_->reset();
        preCmd_->begin();
        gpuLineRenderer_.resetScratch();
        for (auto& p : figure.placements())
            for (auto* plot : p.axes->drawOrder()) {
                if (subset == DrawSubset::StaticOnly && plot->animated)
                    continue;
                if (subset == DrawSubset::AnimatedOnly && !plot->animated)
                    continue;
                const_cast<plot::IPlot*>(plot)->preDraw(
                    preCmd_->handle(), *this, *p.axes, p.axes->rect);
            }
        preCmd_->end();
        vk::SubmitInfo si{};
        vk::CommandBuffer pcb = preCmd_->handle();
        si.setCommandBuffers(pcb);
        ctx.device.graphicsQueue().submit(si);
    }

    auto cmd = subset == DrawSubset::AnimatedOnly
                   ? backend_.beginFrameLoad()
                   : backend_.beginFrame();
    textRenderer_.resetScratch();
    spineRenderer_.resetScratch();
    instancedPathRenderer_.resetScratch();

    // Figure patch (figure.facecolor) fills the canvas under everything.
    const auto& figFc = figure.style().faceColor;
    if (subset != DrawSubset::AnimatedOnly && spineInited_ &&
        figure.style().frameOn && figFc.a > 0.0f) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        plot::Rect2D canvas{0, 0, ext.width, ext.height};
        spineRenderer_.drawFilledRect(cmd, fullRect, ext, canvas, figFc);
    }

    for (const auto* p : figure.axesDrawOrder()) {
        // mpl Axes.set_visible(False) hides the whole axes.
        if (!p->axes->visible()) continue;
        plot::Rect2D rect = p->axes->rect;
        vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}};

        const bool gridOn = p->axes->style().xAxis.grid ||
                            p->axes->style().yAxis.grid;

        // Axes facecolor patch (matplotlib axes.facecolor). Drawn only
        // when both axison and frame_on are set (mpl Axes.draw: the
        // patch is inserted into the draw list iff axison && frameon).
        if (subset != DrawSubset::AnimatedOnly && spineInited_ &&
            p->axes->axison() && p->axes->frameOn()) {
            // mpl Axes.set_alpha scales the patch alpha.
            auto fc = p->axes->style().faceColor;
            if (auto a = p->axes->alpha()) fc.a *= *a;
            if (fc.a > 0.0f)
                spineRenderer_.drawFilledRect(cmd, vrect, ext, rect, fc);
        }

        const bool polar = p->axes->projection().kind ==
                           plot::ProjectionKind::Polar;

        // Draw tick-aligned grid lines (per-axis enable). axisBelow
        // selects whether the grid sits under or over the plot artists.
        // Polar axes draw radial spokes + r-circles instead.
        if (subset != DrawSubset::AnimatedOnly && gridOn &&
            p->axes->style().axisBelow)
            polar ? drawPolarGrid(cmd, *p->axes, rect)
                  : drawGrid(cmd, *p->axes, rect);

        // Draw plot layers in zorder, filtered by the blit subset.
        for (auto* plot : p->axes->drawOrder()) {
            if (!plot->visible) continue;  // mpl set_visible(False)
            if (subset == DrawSubset::StaticOnly && plot->animated) continue;
            if (subset == DrawSubset::AnimatedOnly && !plot->animated) continue;
            const_cast<plot::IPlot*>(plot)->draw(cmd, *this, *p->axes, rect);
        }

        if (subset != DrawSubset::AnimatedOnly && gridOn &&
            !p->axes->style().axisBelow)
            polar ? drawPolarGrid(cmd, *p->axes, rect)
                  : drawGrid(cmd, *p->axes, rect);

        if (subset == DrawSubset::AnimatedOnly) continue;

        // 3D plots (matplotlib projection="3d") draw no 2D spine
        // rectangle — mplot3d renders its own box/panes instead.
        bool has3D = std::ranges::any_of(p->axes->drawOrder(),
            [](const plot::IPlot* pl) { return pl->is3D(); });
        // mpl cax axes (fig.colorbar(cax=)/colorbar.make_axes) have no
        // spines or tick furniture of their own.
        bool isCax = p->axes->style().colorbar.caxMode;

        // Draw axis spines and tick marks. Polar axes get a circular
        // frame plus theta/r labels instead of rectilinear furniture.
        if (polar) drawPolarSpineAndLabels(cmd, *p->axes, rect);
        else if (!has3D && !isCax) drawSpines(cmd, *p->axes, rect);
        // Draw text (axis labels, tick labels, title).
        if (!isCax && textInited_ && textReady_) {
            drawText(cmd, *p->axes, rect);
        }

        // Draw text annotations and arrow annotations.
        if (textInited_ && textReady_) {
            drawAnnotations(cmd, *p->axes, rect);
        }

        // Draw legend (if enabled).
        drawLegend(cmd, *p->axes, rect);

        // Draw anchored size bars.
        if (textInited_ && textReady_)
            drawSizeBars(cmd, *p->axes, rect);

        // Draw anchored text boxes.
        if (textInited_ && textReady_)
            drawAnchoredTexts(cmd, *p->axes, rect);

        // Draw inset-zoom indicators (connectors span the canvas).
        drawInsetIndicators(cmd, *p->axes, rect);

        // Draw colorbar (if enabled).
        drawColorbar(cmd, *p->axes, rect);
    }

    // Figure-level legend (mpl fig.legend) sits above all axes.
    if (subset != DrawSubset::AnimatedOnly)
        drawFigureLegend(cmd, figure);

    // Figure suptitle at top center (mirrors VectorRenderer).
    const auto& ft = figure.style().title;
    if (subset != DrawSubset::AnimatedOnly && textInited_ && textReady_ &&
        !ft.text.empty()) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        float scale = ft.font.size / 12.0f;
        auto m = measureRichText(ft.text, scale);
        auto fm = richTextFace(ft.font);
        drawRichText(cmd, fullRect, ft.text,
                     ext.width * 0.5f - m.width * 0.5f,
                     m.ascent + 2.0f, ft.color, scale,
                     ft.font.rotation, plot::HAlign::Center, &ft.font);
        if ((ft.weight == "bold" || ft.font.weight == "bold") && !fm.bold)
            drawRichText(cmd, fullRect, ft.text,
                         ext.width * 0.5f - m.width * 0.5f + 0.6f,
                         m.ascent + 2.0f, ft.color, scale,
                         ft.font.rotation, plot::HAlign::Center,
                         &ft.font);
    }

    // Figure-level texts (mpl fig.text / fig.texts) — figure-fraction
    // coords by default, honor per-text transform/coords/bbox.
    if (subset != DrawSubset::AnimatedOnly && textInited_ && textReady_) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        plot::Extent2D figExtent{ext.width, ext.height};
        float dpi = figure.style().dpi;
        const float kFS = dpi / (72.0f * 16.0f);
        for (const auto& t : figure.texts()) {
            if (!t.visible || t.detached || t.text.empty()) continue;
            // No axes context: resolve Figure/Display coords directly.
            plot::Point2D pos;
            if (t.transform) pos = t.transform->apply({t.x, t.y});
            else if (t.coords == plot::CoordSystem::Display)
                pos = {t.x, t.y};
            else  // Figure fraction (mpl fig.text default)
                pos = {t.x * ext.width, (1.0f - t.y) * ext.height};
            pos.x += t.dragOffset.x;
            pos.y += t.dragOffset.y;
            auto m = measureRichText(t.text, t.fontSize * kFS);
            if (t.hasBbox || t.bboxFaceColor.a > 0.0f) {
                auto al = plot::alignText(pos, t.halign, t.valign,
                                          m.width, m.height, m.ascent);
                drawTextBbox(cmd, fullRect, ext, spineRenderer_,
                             al.x, al.y - m.ascent,
                             m.width, m.height, t.bboxPadding,
                             t.bboxFaceColor, t.bboxEdgeColor,
                             t.bboxEdgeWidth, t.boxStyle,
                             t.fontSize * dpi / 72.0f);
            }
            auto al = plot::alignText(pos, t.halign, t.valign,
                                      m.width, m.height, m.ascent);
            drawRichTextFx(cmd, fullRect, t.pathEffects, t.text,
                           al.x, al.y, t.color, t.fontSize * kFS,
                           t.rotation, t.halign, dpi, &t.font);
        }
    }

    // Figure-level axis labels (mpl fig.supxlabel / fig.supylabel):
    // bottom-center horizontal and left-center rotated bottom-to-top.
    // Font follows figure.labelsize ('large' ≈ 12pt).
    if (subset != DrawSubset::AnimatedOnly && textInited_ && textReady_ &&
        (!figure.supxlabel().empty() || !figure.supylabel().empty())) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        float dpi = figure.style().dpi;
        float labScale = figure.supXlabelFont.size * dpi /
                         (72.0f * 16.0f);
        if (!figure.supxlabel().empty()) {
            auto m = measureRichText(figure.supxlabel(), labScale);
            drawRichText(cmd, fullRect, figure.supxlabel(),
                         ext.width * 0.5f - m.width * 0.5f,
                         ext.height - 4.0f - m.height + m.ascent,
                         figure.supXlabelColor, labScale, 0.0f,
                         plot::HAlign::Center, &figure.supXlabelFont);
        }
        labScale = figure.supYlabelFont.size * dpi / (72.0f * 16.0f);
        if (!figure.supylabel().empty()) {
            auto m = measureRichText(figure.supylabel(), labScale);
            // Rotated text reads bottom-to-top at the left margin
            // (mpl supylabel at fig fraction (0.02, 0.5)).
            drawRichText(cmd, fullRect, figure.supylabel(),
                         4.0f + m.ascent,
                         ext.height * 0.5f + m.width * 0.5f,
                         figure.supYlabelColor, labScale,
                         -1.5707963267948966f /* -π/2, bottom-to-top */,
                         plot::HAlign::Center, &figure.supYlabelFont);
        }
    }

    // Interactive overlays (§11): widgets + nav zoom rubber-band.
    if (subset != DrawSubset::AnimatedOnly && spineInited_) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        Painter painter{cmd, *this, fullRect};
        for (auto& w : figure.widgets()) w->draw(painter);
        if (figure.navCreated() && figure.nav().hasZoomRect()) {
            auto [a, b] = figure.nav().zoomRect();
            plot::Rect2D zr{int32_t(std::lround(std::min(a.x, b.x))),
                            int32_t(std::lround(std::min(a.y, b.y))),
                            uint32_t(std::lround(std::abs(b.x - a.x))),
                            uint32_t(std::lround(std::abs(b.y - a.y)))};
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
    // mpl colorbar tick labels + label use font.size pt at figure dpi.
    const float cbDpi =
        axes.figure() ? axes.figure()->dpi() : style.dpi;
    const float cbScale = style.fontSize * cbDpi / (72.0f * 16.0f);

    // The colorbar is tied to its mappable's value range (matplotlib:
    // the mappable's norm); default is the first colormap-mapped plot.
    // Fall back to the z viewport for 3D-style plots that don't expose
    // a scalar range.
    const auto& cbsEarly = style.colorbar;
    float valueMin = 0.0f, valueMax = 1.0f;
    bool hasRange = false;
    if (cbsEarly.mappable) {
        if (auto vr = cbsEarly.mappable->valueRange(); vr && vr->valid()) {
            valueMin = vr->min; valueMax = vr->max; hasRange = true;
        }
    }
    if (!hasRange && cbsEarly.explicitRange &&
        cbsEarly.explicitRange->valid()) {
        valueMin = cbsEarly.explicitRange->min;
        valueMax = cbsEarly.explicitRange->max;
        hasRange = true;
    }
    if (!hasRange)
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
    // mpl cax: the axes' own rect IS the colorbar region (strip +
    // tick-label space), no pad-relative placement.
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
    // mpl: extension length = extendfrac·strip-length (auto → 0.05).
    float extFrac = cbs.extendfrac > 0.0f ? cbs.extendfrac : 0.05f;
    float extH = extFrac * stripH;
    float bodyY0 = stripY + (extMax ? extH : 0.0f);
    float bodyY1 = stripY + stripH - (extMin ? extH : 0.0f);
    float bodyH = bodyY1 - bodyY0;

    // Colormap/norm resolution (mpl): the mappable's cmap/norm win over
    // the style's name/explicit norm, which wins over the default.
    auto mappableNorm = cbs.mappable ? cbs.mappable->norm() : nullptr;
    const plot::Colormap& cmap =
        cbs.cmapPtr ? *cbs.cmapPtr
        : (cbs.mappable && cbs.mappable->cmap() ? *cbs.mappable->cmap()
           : plot::Colormap::byName(style.colorbar.colormap));
    const plot::Normalize* effNorm =
        cbs.norm ? cbs.norm.get() : mappableNorm.get();
    auto sampleAt = [&](float t) -> plot::Color {
        plot::Color c;
        if (effNorm) {
            float v = valueMin + t * (valueMax - valueMin);
            c = cmap.sample((*effNorm)(v));
        } else {
            c = cmap.sample(t);
        }
        c.a *= cbs.alpha; // mpl fig.colorbar(alpha=)
        return c;
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
        // mpl: extension length = extendfrac·strip-length (auto→0.05).
        float extW = extFrac * stripW;
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
        if (extMin) {   // left-pointing extension
            if (cbs.extendrect) {
                spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                    {int32_t(bodyX0 - extW), int32_t(stripY),
                     uint32_t(extW), uint32_t(stripH)},
                    sampleAt(0.0f));
            } else {
                plot::Point2D tri[3] = {
                    {bodyX0, stripY}, {bodyX0, stripY + stripH},
                    {bodyX0 - extW, stripY + stripH * 0.5f}};
                spineRenderer_.drawTriangles(cmd, fullRect, res2, tri,
                                             sampleAt(0.0f));
            }
        }
        if (extMax) {   // right-pointing extension
            if (cbs.extendrect) {
                spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                    {int32_t(bodyX1), int32_t(stripY),
                     uint32_t(extW), uint32_t(stripH)},
                    sampleAt(1.0f));
            } else {
                plot::Point2D tri[3] = {
                    {bodyX1, stripY}, {bodyX1, stripY + stripH},
                    {bodyX1 + extW, stripY + stripH * 0.5f}};
                spineRenderer_.drawTriangles(cmd, fullRect, res2, tri,
                                             sampleAt(1.0f));
            }
        }
        spineRenderer_.drawRect(cmd, fullRect, ext,
            {int32_t(bodyX0), int32_t(stripY),
             uint32_t(bodyW), uint32_t(stripH)},
            style.colorbar.edgeColor, 1.0f);

        // mpl Colorbar.set_ticks overrides auto-located ticks;
        // set_ticklabels replaces the formatted labels positionally;
        // log/symlog norms get locator ticks via colorbarTicks.
        auto cbt = colorbarTicks(cbs.ticks, cbs.tickLabels,
                                 cbs.minorTicksOn, effNorm,
                                 valueMin, valueMax, cbs.format);
        for (size_t ti = 0; ti < cbt.majors.size(); ++ti) {
            float tick = cbt.majors[ti];
            float t = effNorm ? (*effNorm)(tick)
                               : (tick - valueMin) / (valueMax - valueMin);
            if (t < 0.0f || t > 1.0f) continue;   // mpl clips to the bar
            float x = bodyX0 + t * bodyW;
            plot::Point2D tickPts[2] = {
                {x, stripY + stripH}, {x, stripY + stripH + 4.0f}};
            spineRenderer_.drawLineStrip(cmd, fullRect, ext, tickPts,
                                         style.colorbar.edgeColor, 1.0f);
            drawRichText(cmd, fullRect, cbt.labels[ti],
                         x - 8.0f, stripY + stripH + 14.0f,
                         style.colorbar.labelColor, cbScale);
        }
        // Minor marks from colorbarTicks (log subs or /5 divisions).
        for (float mv : cbt.minors) {
            float t = effNorm
                ? (*effNorm)(mv)
                : (mv - valueMin) / (valueMax - valueMin);
            if (t <= 0.0f || t >= 1.0f) continue;
            float x = bodyX0 + t * bodyW;
            plot::Point2D mTick[2] = {
                {x, stripY + stripH},
                {x, stripY + stripH + 2.0f}};
            spineRenderer_.drawLineStrip(cmd, fullRect, ext, mTick,
                                style.colorbar.edgeColor, 1.0f);
        }
        // mpl colorbar.set_label — centered under the horizontal strip.
        if (!cbs.label.empty()) {
            auto m = textRenderer_.measureText(cbs.label, cbScale);
            textRenderer_.draw(cmd, fullRect, cbs.label,
                               bodyX0 + bodyW * 0.5f - m.width * 0.5f,
                               stripY + stripH + 30.0f,
                               style.colorbar.labelColor, cbScale);
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
        // mpl extendrect → rectangular extension; else downward
        // triangle under the strip.
        if (cbs.extendrect) {
            spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                {int32_t(stripX), int32_t(bodyY1),
                 uint32_t(stripW), uint32_t(extH)},
                sampleAt(0.0f));
        } else {
            plot::Point2D tri[3] = {
                {stripX, bodyY1}, {stripX + stripW, bodyY1},
                {stripX + stripW * 0.5f, bodyY1 + extH}};
            spineRenderer_.drawTriangles(cmd, fullRect, res, tri,
                                         sampleAt(0.0f));
        }
    }
    if (extMax) {
        if (cbs.extendrect) {
            spineRenderer_.drawFilledRect(cmd, fullRect, ext,
                {int32_t(stripX), int32_t(bodyY0 - extH),
                 uint32_t(stripW), uint32_t(extH)},
                sampleAt(1.0f));
        } else {
            plot::Point2D tri[3] = {
                {stripX, bodyY0}, {stripX + stripW, bodyY0},
                {stripX + stripW * 0.5f, bodyY0 - extH}};
            spineRenderer_.drawTriangles(cmd, fullRect, res, tri,
                                         sampleAt(1.0f));
        }
    }

    // Draw border around the strip body (plus extend outlines).
    spineRenderer_.drawRect(cmd, fullRect, ext,
        {int32_t(stripX), int32_t(bodyY0), uint32_t(stripW), uint32_t(bodyH)},
        style.colorbar.edgeColor, 1.0f);

    // Draw tick labels — positioned on the strip body (inside extends);
    // when a custom norm is set, tick t goes through it (matplotlib norm).
    // mpl Colorbar.set_ticks/set_ticklabels override the auto ticks;
    // log/symlog norms get locator-appropriate ticks (colorbarTicks).
    auto cbt = colorbarTicks(cbs.ticks, cbs.tickLabels, cbs.minorTicksOn,
                             effNorm, valueMin, valueMax, cbs.format);
    for (size_t ti = 0; ti < cbt.majors.size(); ++ti) {
        float tick = cbt.majors[ti];
        float t = effNorm ? (*effNorm)(tick)
                           : (tick - valueMin) / (valueMax - valueMin);
        if (t < 0.0f || t > 1.0f) continue;   // mpl clips to the bar
        float y = bodyY0 + (1.0f - t) * bodyH;  // top = max, bottom = min
        // Draw tick mark.
        plot::Point2D tickPts[2] = {
            {stripX + stripW, y},
            {stripX + stripW + 4.0f, y},
        };
        spineRenderer_.drawLineStrip(cmd, fullRect, ext, tickPts,
                                     style.colorbar.edgeColor, 1.0f);
        // Draw label (rich text for "$10^{k}$" mathtext labels).
        drawRichText(cmd, fullRect, cbt.labels[ti],
                     stripX + stripW + 8.0f, y + 6.0f,
                     style.colorbar.labelColor, cbScale);
    }
    // Minor marks: locator subs for log norms, /5 subdivisions for
    // linear norms with minorticks_on (colorbarTicks supplies them).
    for (float mv : cbt.minors) {
        float t = effNorm
            ? (*effNorm)(mv)
            : (mv - valueMin) / (valueMax - valueMin);
        if (t <= 0.0f || t >= 1.0f) continue;
        float y = bodyY0 + (1.0f - t) * bodyH;
        plot::Point2D mTick[2] = {
            {stripX + stripW, y},
            {stripX + stripW + 2.0f, y}};
        spineRenderer_.drawLineStrip(cmd, fullRect, ext, mTick,
                            style.colorbar.edgeColor, 1.0f);
    }

    // mpl colorbar.set_label — rotated alongside a vertical strip,
    // right of the tick labels.
    if (!cbs.label.empty()) {
        auto m = textRenderer_.measureText(cbs.label, cbScale);
        textRenderer_.draw(cmd, fullRect, cbs.label,
                           stripX + stripW + 26.0f,
                           bodyY0 + bodyH * 0.5f + m.width * 0.5f,
                           style.colorbar.labelColor, cbScale,
                           -float(M_PI) / 2.0f);
    }
}

void Renderer::drawAnnotations(vk::CommandBuffer cmd, const plot::Axes& axes,
                                plot::Rect2D rect) {
    const auto& vp = axes.viewport();
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    plot::Extent2D figExtent{ext.width, ext.height};
    float dpi = figDpi;
    float baseFontSize = 16.0f;

    // --- Text annotations (ax.text) ---
    for (const auto& t : axes.texts()) {
        if (!t.visible || t.detached || t.text.empty()) continue;
        // mpl transform= overrides the coord system entirely.
        auto pos = t.transform
            ? t.transform->apply({t.x, t.y})
            : plot::toDisplay(t.x, t.y, t.coords, rect, figExtent, axes,
                              dpi, t.xyOffsetX, t.xyOffsetY);
        pos.x += t.dragOffset.x;
        pos.y += t.dragOffset.y;
        float scale = t.fontSize * dpi / (72.0f * 16.0f);
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

        // Draw background box if requested (mpl FancyBboxPatch).
        if (t.hasBbox || t.bboxFaceColor.a > 0.0f) {
            auto aligned = plot::alignText(pos, t.halign, t.valign,
                                           m.width, m.height, m.ascent);
            drawTextBbox(cmd, clipRect, backend_.extent(), spineRenderer_,
                         aligned.x, aligned.y - m.ascent,
                         m.width, m.height, t.bboxPadding,
                         t.bboxFaceColor, t.bboxEdgeColor,
                         t.bboxEdgeWidth, t.boxStyle,
                         t.fontSize * dpi / 72.0f);
        }

        auto drawPos = plot::alignText(pos, t.halign, t.valign,
                                       m.width, m.height, m.ascent);
        drawRichTextFx(cmd, clipRect, t.pathEffects, t.text,
                       drawPos.x, drawPos.y, t.color, scale, t.rotation,
                       t.halign, dpi, &t.font);
    }

    // --- Arrow annotations (ax.annotate) ---
    for (const auto& a : axes.annotations()) {
        if (a.detached) continue;
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
            float scale = a.fontSize * dpi / (72.0f * 16.0f);
            auto m = measureRichText(a.text, scale);
            {
                auto al = plot::alignText(textPos, a.halign, a.valign,
                                          m.width, m.height, m.ascent);
                a.drawBox = {int32_t(al.x), int32_t(al.y - m.ascent),
                             uint32_t(m.width), uint32_t(m.height)};
            }

            // Draw background box if requested (mpl FancyBboxPatch).
            if (a.hasBbox || a.bboxFaceColor.a > 0.0f) {
                auto aligned = plot::alignText(textPos, a.halign, a.valign,
                                               m.width, m.height, m.ascent);
                drawTextBbox(cmd, clipRect, backend_.extent(),
                             spineRenderer_,
                             aligned.x, aligned.y - m.ascent,
                             m.width, m.height, a.bboxPadding,
                             a.bboxFaceColor, a.bboxEdgeColor,
                             a.bboxEdgeWidth, a.boxStyle,
                             a.fontSize * dpi / 72.0f);
            }

            auto drawPos = plot::alignText(textPos, a.halign, a.valign,
                                           m.width, m.height, m.ascent);
            drawRichTextFx(cmd, clipRect, a.pathEffects, a.text,
                           drawPos.x, drawPos.y, a.color, scale, 0.0f,
                           a.halign, dpi, &a.font);
        }
    }
}

bool Renderer::savefig(plot::Figure& figure,
                       const std::filesystem::path& path,
                       const encode::SaveOptions& options) {
    // The canvas was rendered at figure.dpi — savefig(dpi=) scales the
    // output relative to that, not a fixed 100.
    encode::SaveOptions opts = options;
    opts.canvasDpi = figure.style().dpi;
    auto fmt = opts.format
        ? *opts.format
        : encode::formatFromPath(path).value_or(encode::ImageFormat::Png);
    switch (fmt) {
    case encode::ImageFormat::Pdf:
    case encode::ImageFormat::Svg:
    case encode::ImageFormat::Eps:
    case encode::ImageFormat::Pgf:
        return savefigVector(figure, path, opts, fmt);
    default: break;
    }

    // mpl savefig(transparent=True): every Axes patch and the Figure
    // patch become transparent unless facecolor is given (mpl applies
    // facecolor via kwargs.setdefault → it wins over 'none').
    auto savedFc = figure.style().faceColor;
    std::vector<plot::Color> savedAxFc;
    if (opts.transparent) {
        savedAxFc.reserve(figure.placements().size());
        for (auto& p : figure.placements()) {
            savedAxFc.push_back(p.axes->style().faceColor);
            p.axes->style().faceColor.a = 0.0f;
        }
    }
    if (opts.facecolor) {
        const auto& fc = *opts.facecolor;
        backend_.setClearColor(fc[0], fc[1], fc[2], fc[3]);
        figure.style().faceColor = plot::Color{fc[0], fc[1], fc[2], fc[3]};
    } else if (opts.transparent) {
        backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        figure.style().faceColor.a = 0.0f;
    }
    // mpl frameon=False: the figure patch isn't drawn, leaving the
    // canvas transparent (not the white clear color).
    const bool frameless = !figure.style().frameOn;
    if (frameless) backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    prepare(figure);
    renderFrame(figure);

    // Restore the clear color and patches for subsequent frames.
    if (opts.transparent || opts.facecolor || frameless) {
        backend_.setClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        figure.style().faceColor = savedFc;
        size_t i = 0;
        for (auto& p : figure.placements()) {
            if (i >= savedAxFc.size()) break;
            p.axes->style().faceColor = savedAxFc[i++];
        }
    }

    auto pixels = backend_.readbackRgba8();
    if (pixels.empty()) return false;
    auto ext = backend_.extent();
    return encode::saveImage(pixels, ext.width, ext.height, path, opts);
}

namespace {

/// VectorCanvas that only accumulates drawn-content bounds — used by
/// savefigVector for `bbox_inches="tight"`. Discards all output.
class BoundsCanvas final : public VectorCanvas {
public:
    explicit BoundsCanvas(VectorRenderer::MeasureFn measure)
        : measure_(std::move(measure)) {}

    void polyline(std::span<const plot::Point2D> pts,
                  const Pen& pen) override {
        grow(pts, pen.width * 0.5f);
    }
    void polygon(std::span<const plot::Point2D> pts, plot::Color,
                 const Pen* stroke) override {
        grow(pts, stroke ? stroke->width * 0.5f : 0.0f);
    }
    void text(plot::Point2D base, std::string_view s, float sizePx,
              plot::Color, float rot) override {
        if (!measure_) { grow(base.x, base.y, 0.0f); return; }
        auto m = measure_(s, sizePx / 16.0f);
        float desc = m.height - m.ascent;
        float cs = std::cos(rot), sn = std::sin(rot);
        for (auto [dx, dy] : {std::pair{0.0f, -m.ascent},
                              {m.width, -m.ascent},
                              {m.width, desc}, {0.0f, desc}})
            grow(base.x + dx * cs - dy * sn,
                 base.y + dx * sn + dy * cs, 0.0f);
    }
    void image(plot::Rect2D r, uint32_t, uint32_t,
               std::span<const uint8_t>) override {
        grow(r.x, r.y, 0.0f);
        grow(r.x + r.width, r.y + r.height, 0.0f);
    }
    void pushClip(plot::Rect2D) override {}
    void popClip() override {}
    bool finish(const std::filesystem::path&) override { return true; }

    bool empty = true;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;

private:
    void grow(float x, float y, float pad) {
        if (empty) { x0 = x1 = x; y0 = y1 = y; empty = false; }
        x0 = std::min(x0, x - pad); x1 = std::max(x1, x + pad);
        y0 = std::min(y0, y - pad); y1 = std::max(y1, y + pad);
    }
    void grow(std::span<const plot::Point2D> pts, float pad) {
        for (auto p : pts) grow(p.x, p.y, pad);
    }
    VectorRenderer::MeasureFn measure_;
};

/// VectorCanvas decorator translating all emitted geometry by (dx, dy).
class ShiftCanvas final : public VectorCanvas {
public:
    ShiftCanvas(VectorCanvas& inner, float dx, float dy)
        : inner_(inner), dx_(dx), dy_(dy) {}

    void polyline(std::span<const plot::Point2D> pts,
                  const Pen& pen) override {
        scratch_.assign(pts.begin(), pts.end());
        for (auto& p : scratch_) { p.x += dx_; p.y += dy_; }
        inner_.polyline(scratch_, pen);
    }
    void polygon(std::span<const plot::Point2D> pts, plot::Color fill,
                 const Pen* stroke) override {
        scratch_.assign(pts.begin(), pts.end());
        for (auto& p : scratch_) { p.x += dx_; p.y += dy_; }
        inner_.polygon(scratch_, fill, stroke);
    }
    void text(plot::Point2D base, std::string_view s, float sizePx,
              plot::Color c, float rot) override {
        inner_.text({base.x + dx_, base.y + dy_}, s, sizePx, c, rot);
    }
    void image(plot::Rect2D r, uint32_t w, uint32_t h,
               std::span<const uint8_t> rgba) override {
        inner_.image({int32_t(std::lround(r.x + dx_)),
                      int32_t(std::lround(r.y + dy_)),
                      r.width, r.height}, w, h, rgba);
    }
    void pushClip(plot::Rect2D r) override {
        inner_.pushClip({int32_t(std::lround(r.x + dx_)),
                         int32_t(std::lround(r.y + dy_)),
                         r.width, r.height});
    }
    void popClip() override { inner_.popClip(); }
    bool finish(const std::filesystem::path& p) override {
        return inner_.finish(p);
    }

private:
    VectorCanvas& inner_;
    float dx_, dy_;
    std::vector<plot::Point2D> scratch_;
};

} // namespace

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
    vo.facecolor = !figure.style().frameOn ||
                           options.transparent
                       ? plot::Color{0, 0, 0, 0}
                   : options.facecolor
                       ? plot::Color{(*options.facecolor)[0],
                                     (*options.facecolor)[1],
                                     (*options.facecolor)[2],
                                     (*options.facecolor)[3]}
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
            ++frameSeq_;   // invalidate preDraw GPU meshes from prior frames
            backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            auto cmd = backend_.beginFrame();
            spineRenderer_.resetScratch();
            instancedPathRenderer_.resetScratch();
            textRenderer_.resetScratch();
            for (auto* p : plots)
                if (p->visible)
                    const_cast<plot::IPlot*>(p)->draw(cmd, *this, axes,
                                                      axes.rect);
            backend_.endFrame();
            backend_.setClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            rgba = backend_.readbackRgba8();
            auto e = backend_.extent();
            w = e.width; h = e.height;
            return !rgba.empty();
        };
    auto probeMeasure = measure;  // copy — measure moves into vr below
    VectorRenderer vr{std::move(measure), std::move(rasterize),
                      plot::Extent2D{ext.width, ext.height}};

    // mpl transparent=True also clears every Axes patch (facecolor
    // 'none'), not just the figure patch.
    std::vector<plot::Color> savedAxFc;
    if (options.transparent) {
        savedAxFc.reserve(figure.placements().size());
        for (auto& p : figure.placements()) {
            savedAxFc.push_back(p.axes->style().faceColor);
            p.axes->style().faceColor.a = 0.0f;
        }
    }
    auto restoreAxFc = [&] {
        size_t i = 0;
        for (auto& p : figure.placements()) {
            if (i >= savedAxFc.size()) break;
            p.axes->style().faceColor = savedAxFc[i++];
        }
    };

    // bbox_inches="tight": pass 1 records content bounds; pass 2 emits
    // into a tight-sized canvas shifted by (-x0+pad, -y0+pad).
    if (options.tight) {
        BoundsCanvas probe(std::move(probeMeasure));
        vr.render(figure, probe);
        if (!probe.empty) {
            // Pad in canvas pixels: pad_inches × the canvas's own dpi
            // (the vector canvas is sized in canvas px).
            float padPx = options.padInches * options.canvasDpi;
            float w = probe.x1 - probe.x0 + 2.0f * padPx;
            float h = probe.y1 - probe.y0 + 2.0f * padPx;
            auto canvas = vectorCanvas(vf, w, h, vo);
            if (!canvas) { restoreAxFc(); return false; }
            ShiftCanvas shifted(*canvas, padPx - probe.x0,
                                padPx - probe.y0);
            vr.render(figure, shifted);
            restoreAxFc();
            return canvas->finish(path);
        }
    }

    auto canvas = vectorCanvas(vf, float(ext.width), float(ext.height), vo);
    if (!canvas) { restoreAxFc(); return false; }
    vr.render(figure, *canvas);
    restoreAxFc();
    return canvas->finish(path);
}

bool Renderer::saveAnimation(plot::Animation& anim,
                             const std::filesystem::path& path, double fps,
                             std::string_view writerName) {
    auto w = encode::createMovieWriter(writerName, path);
    if (!w) return false;
    auto ext = backend_.extent();
    if (!w->open(path, ext.width, ext.height, fps)) return false;
    // GPU-side PNG filtering for APNG frames (compute shader picks the
    // per-row filter; zlib deflate stays on the CPU).
    std::unique_ptr<encode::GpuPngEncoder> gpuEnc;
    if (auto* apng = dynamic_cast<encode::ApngWriter*>(w.get())) {
        auto& ctx = backend_.context();
        gpuEnc = std::make_unique<encode::GpuPngEncoder>(
            ctx.device.handle(), ctx.device.graphicsQueue(),
            ctx.graphicsPool.handle(), ctx.allocator.handle());
        auto* enc = gpuEnc.get();
        apng->setFrameFilter([enc](std::span<const uint8_t> rgba,
                                   uint32_t fw, uint32_t fh) {
            return enc->filterScanlines(rgba, fw, fh);
        });
    }
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
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    if (axes.sizeBars().empty()) return;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    const float dpi = figDpi;
    auto measureFn = [&](std::string_view t, float pt) {
        auto m = measureRichText(t, pt * dpi / (72.0f * 16.0f));
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    auto toI = [](plot::Rect2Df r) {
        return plot::Rect2D{int32_t(std::lround(r.x)),
                            int32_t(std::lround(r.y)),
                            uint32_t(std::lround(r.w)),
                            uint32_t(std::lround(r.h))};
    };
    for (const auto& sb : axes.sizeBars()) {
        if (sb.detached) continue;
        auto L = plot::layoutSizeBar(sb, axes, rect, {ext.width, ext.height},
                                     dpi, measureFn);
        if (!L.valid) continue;
        if (sb.frameon) {
            spineRenderer_.drawFilledRect(cmd, fullRect, ext, toI(L.box),
                                          sb.frameFaceColor);
            spineRenderer_.drawRect(cmd, fullRect, ext, toI(L.box),
                                    sb.frameEdgeColor, 1.0f);
        }
        if (L.fill)
            spineRenderer_.drawFilledRect(cmd, fullRect, ext, toI(L.bar),
                                          sb.color);
        else
            spineRenderer_.drawRect(cmd, fullRect, ext, toI(L.bar),
                                    sb.color, 1.0f);
        if (!sb.label.empty())
            drawRichText(cmd, fullRect, sb.label, L.labelBaseline.x,
                         L.labelBaseline.y, sb.color,
                         sb.fontSize * dpi / (72.0f * 16.0f));
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
    const float figDpi = axes.figure()
        ? axes.figure()->dpi() : axes.style().dpi;
    if (axes.anchoredTexts().empty()) return;
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    const float dpi = figDpi;
    auto measureFn = [&](std::string_view t, float pt) {
        auto m = measureRichText(t, pt * dpi / (72.0f * 16.0f));
        return plot::SizeBarTextMeasure{m.width, m.height, m.ascent};
    };
    for (const auto& at : axes.anchoredTexts()) {
        if (at.detached) continue;
        auto L = plot::layoutAnchoredText(at, axes, rect, dpi, measureFn);
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
                         L.lineBaselines[i].y, at.color,
                         at.fontSize * dpi / (72.0f * 16.0f),
                         0.0f, plot::HAlign::Left, &at.font);
            ++i;
            if (nl == std::string_view::npos) break;
            start = nl + 1;
        }
    }
}

} // namespace volcano::render
