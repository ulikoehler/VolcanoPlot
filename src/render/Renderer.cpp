// volcano/render/Renderer.cpp
#include "volcano/render/Renderer.hpp"
#include <volcano/encode/MovieWriter.hpp>
#include <volcano/plot/Animation.hpp>
#include <volcano/plot/Colormap.hpp>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Interaction.hpp>
#include <volcano/plot/Ticks.hpp>
#include <volcano/plot/Widgets.hpp>
#include <volcano/text/MathText.hpp>

#include <algorithm>
#include <cmath>
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
        r_.spineRenderer().drawFilledRect(cmd_, scissor_, rect, c);
    }
    void outlineRect(plot::Rect2D rect, plot::Color c, float w) override {
        r_.spineRenderer().drawRect(cmd_, scissor_, rect, c, w);
    }
    void line(std::span<const plot::Point2D> pts, plot::Color c,
              float w) override {
        r_.spineRenderer().drawLineStrip(cmd_, scissor_, pts, c, w);
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

/// Simple "nice number" tick locator (matplotlib MaxNLocator style).
/// Returns ~nbins tick positions within [vmin, vmax].
/// matplotlib's MaxNLocator with default steps=[1,2,5,10] picks the
/// nice step that gives at most nbins ticks.
std::vector<float> autoTicks(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return {};
    float range = vmax - vmin;
    // matplotlib's MaxNLocator tries steps [1, 2, 2.5, 5, 10] × 10^k and picks
    // the smallest step that gives at most nbins+1 ticks (i.e. the most
    // ticks without exceeding nbins).
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    // Try nice steps from smallest to largest, pick the FIRST one that
    // gives <= nbins+1 ticks. This maximizes the number of ticks.
    float niceSteps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
    float niceStep = 10.0f * mag;  // fallback: largest step
    for (float s : niceSteps) {
        float step = s * mag;
        int numTicks = int(std::floor(vmax / step) - std::ceil(vmin / step)) + 1;
        if (numTicks <= nbins + 1) {
            niceStep = step;
            break;  // first (smallest) step that fits
        }
    }

    float start = std::ceil(vmin / niceStep) * niceStep;
    std::vector<float> ticks;
    for (float v = start; v <= vmax + niceStep * 0.001f; v += niceStep) {
        // Round to avoid floating-point drift accumulating.
        float k = std::round(v / niceStep);
        ticks.push_back(k * niceStep);
    }
    return ticks;
}

/// Format a tick value as a short string, using the step size to determine
/// the appropriate number of decimal places (matching matplotlib's ScalarFormatter).
std::string formatTick(float v, float step) {
    // Normalize -0.0f to 0.0f to avoid "-0.0" in output.
    if (v == 0.0f) v = std::abs(v);
    // Very large or very small ranges use scientific notation.
    if (std::abs(v) >= 10000.0f || (std::abs(v) < 0.001f && step < 0.001f)) {
        return std::format("{:.1e}", v);
    }
    // Determine decimal places from the step size.
    // Use enough decimal places to represent the step exactly.
    // e.g., step=0.5 → 1 dp, step=0.25 → 2 dp, step=0.1 → 1 dp.
    float stepMag = std::abs(step);
    if (stepMag >= 1.0f) {
        // Integer steps: no decimal places.
        return std::format("{:.0f}", v);
    }
    // Find the minimum decimals where round(step, d) == step.
    int decimals = 0;
    while (decimals < 6) {
        float scale = std::pow(10.0f, decimals);
        float rounded = std::round(stepMag * scale) / scale;
        if (std::abs(rounded - stepMag) < stepMag * 0.01f) break;
        ++decimals;
    }
    return std::format("{:.{}f}", v, decimals);
}

/// Compute the nice step size used by autoTicks.
float autoTickStep(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return 1.0f;
    float range = vmax - vmin;
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    float niceSteps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
    float niceStep = 10.0f * mag;
    for (float s : niceSteps) {
        float step = s * mag;
        int numTicks = int(std::floor(vmax / step) - std::ceil(vmin / step)) + 1;
        if (numTicks <= nbins + 1) {
            niceStep = step;
            break;
        }
    }
    return niceStep;
}

// ─── Locator/formatter plumbing (matplotlib ticker framework) ───────────────

/// Major tick positions honoring TickConfig locator/positions overrides.
std::vector<float> axisTicks(const plot::TickConfig& tc,
                             const plot::AxisScale& scale,
                             float lo, float hi) {
    if (tc.locator) return tc.locator->tickValues(lo, hi);
    if (tc.positions && !tc.positions->empty()) return *tc.positions;
    return plot::scaleTicks(scale, lo, hi, tc.nbins);
}

/// Log-family scales get automatic minor ticks (matplotlib behavior).
bool isLogish(plot::ScaleKind k) {
    using SK = plot::ScaleKind;
    return k == SK::Log || k == SK::Symlog || k == SK::Logit ||
           k == SK::Asinh || k == SK::FunctionLog;
}

/// Whether minor tick marks are drawn for this axis.
bool minorEnabled(const plot::TickConfig& tc, const plot::AxisScale& s) {
    return tc.minor || tc.minorLocator != nullptr ||
           s.kind == plot::ScaleKind::Log || s.kind == plot::ScaleKind::Logit;
}

/// Minor tick positions: explicit minorLocator, log-family subs, or
/// linear subdivisions between majors (AutoMinorLocator).
std::vector<float> axisMinorTicks(const plot::TickConfig& tc,
                                  const plot::AxisScale& scale,
                                  float lo, float hi,
                                  std::span<const float> majors) {
    if (!minorEnabled(tc, scale)) return {};
    if (tc.minorLocator) return tc.minorLocator->tickValues(lo, hi);
    if (scale.kind == plot::ScaleKind::Log ||
        scale.kind == plot::ScaleKind::FunctionLog)
        return plot::LogLocator{}.minorValues(lo, hi);
    if (isLogish(scale.kind)) return {};
    return plot::AutoMinorLocator{}.between(majors, lo, hi);
}

/// Formatter for an axis: explicit formatter, legacy format string, or
/// default ScalarFormatter (setLocs gives offset/scientific detection).
plot::Formatter* axisFormatter(const plot::TickConfig& tc,
                               std::span<const float> ticks,
                               const plot::FigureStyle& style,
                               plot::ScalarFormatter& defaultFmt,
                               plot::FormatStrFormatter& strFmt) {
    plot::Formatter* f = tc.formatter.get();
    if (!f && tc.format != "%g") {
        strFmt = plot::FormatStrFormatter(tc.format);
        f = &strFmt;
    }
    if (!f) {
        defaultFmt = plot::ScalarFormatter{};
        defaultFmt.scilimits = style.formatterLimits;
        defaultFmt.useOffset = style.formatterUseOffset;
        defaultFmt.useMathText = style.formatterUseMathText;
        f = &defaultFmt;
    }
    f->setLocs(ticks);
    return f;
}

/// Label for tick `t` at index `i`, honoring fixed labels first.
std::string tickLabel(const plot::TickConfig& tc, const plot::Formatter& f,
                      float t, int i) {
    if (!tc.formatter && tc.positions && tc.labels &&
        tc.positions->size() == tc.labels->size()) {
        for (size_t k = 0; k < tc.positions->size(); ++k)
            if (std::abs((*tc.positions)[k] - t) < 1e-6f)
                return (*tc.labels)[k];
    }
    return f.format(t, i);
}

/// TickConfig direction string → fraction of tick length inside the axes.
float tickInFrac(const plot::TickConfig& tc) {
    if (tc.direction == "in") return 1.0f;
    if (tc.direction == "inout") return 0.5f;
    return 0.0f; // "out"
}

/// Tick size/width are configured in points; render at ~96dpi (pt → px).
constexpr float kPtToPx = 96.0f / 72.0f;

} // namespace

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
    // Init grid renderer once.
    if (!gridInited_) {
        gridRenderer_.init(ctx.device.handle(), backend_.renderPass(),
                           backend_.sampleCount(), *pipelineCache_,
                           ctx.allocator.handle(),
                           ctx.device.graphicsQueue(),
                           ctx.graphicsPool.handle());
        gridInited_ = true;
    }
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
        spineRenderer_.drawLineStrip(cmd, scissor, std::span{pts, 2},
                                     color, rl.thickness);
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
    float fontSize = 16.0f;
    float scale = 1.0f;
    auto labelColor = style.xAxis.color;

    // Use the full framebuffer as the scissor rect so text outside the
    // axes rect (labels, title) is not clipped.
    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Tick mark geometry constants (shared with drawSpines).
    constexpr float kTickLength = 4.0f;
    constexpr float kTickSpacing = 4.0f;  // gap between tick mark and label

    // --- X axis label ---
    if (style.xAxis.visible && !style.xAxis.label.empty()) {
        auto m = measureRichText(style.xAxis.label, scale);
        // Center horizontally at axes center, below the tick labels.
        // Top border at tickBottom + kTickSpacing + labelHeight + labelGap.
        constexpr float kXLabelGap = 8.0f;
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        float cy = float(rect.y + rect.height) + kTickLength + kTickSpacing +
                   16.0f + kXLabelGap + m.ascent;  // 16px approx label height
        drawRichText(cmd, fullRect,
            style.xAxis.label, cx, cy, labelColor, scale,
            style.xAxis.labelFont.rotation, plot::HAlign::Center);
    }

    // --- Y axis label ---
    if (style.yAxis.visible && !style.yAxis.label.empty()) {
        // Rotate -90° (clockwise in screen space, Y-down) so the label
        // reads bottom-to-top. The rotation origin is the text baseline (x, y).
        // After rotation:
        //   - text width becomes vertical extent (upward from origin)
        //   - ascent becomes leftward extent, descent becomes rightward
        // We want: vertical center at axes middle, positioned left of tick labels.
        auto m = measureRichText(style.yAxis.label, scale);
        // Y position: y - width/2 = axes vertical center
        float oy = rect.y + rect.height / 2.0f + m.width / 2.0f;
        // X position: center of rotated text at (rect.x - tickLen - spacing - maxLabelW - labelGap)
        // Center after rotation = x + (descent - ascent)/2 = x + (m.height - m.ascent - m.ascent)/2
        //                      = x + m.height/2 - m.ascent
        // So x = centerPos - m.height/2 + m.ascent
        constexpr float kYLabelGap = 8.0f;
        float centerPos = float(rect.x) - kTickLength - kTickSpacing - 40.0f - kYLabelGap;
        float ox = centerPos - m.height / 2.0f + m.ascent;
        constexpr float kRotMinus90 = -1.5707963267948966f; // -π/2
        drawRichText(cmd, fullRect,
            style.yAxis.label, ox, oy, labelColor, scale,
            kRotMinus90 + style.yAxis.labelFont.rotation, plot::HAlign::Center);
    }

    // --- Title ---
    if (!style.title.text.empty()) {
        auto m = measureRichText(style.title.text, scale);
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        // Position above the axes: text bottom at rect.y - padding.
        // text bottom = y + descent = y + (height - ascent)
        // So y = rect.y - pad - (height - ascent) = rect.y - pad - height + ascent
        constexpr float kTitlePad = 6.0f;
        float cy = float(rect.y) - kTitlePad - m.height + m.ascent;
        drawRichText(cmd, fullRect,
            style.title.text, cx, cy, style.title.color, scale,
            style.title.font.rotation, plot::HAlign::Center);
    }

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
    if (style.xAxis.visible) {
        const auto& tc = style.xAxis.ticks;
        auto xTicks = axisTicks(tc, axes.xscale(), vp.x.min, vp.x.max);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::Formatter* fmt = axisFormatter(tc, xTicks, style,
                                             defaultFmt, strFmt);
        bool top = axes.xTicksTop();
        float edge = top ? float(rect.y) : float(rect.y + rect.height);
        float d = top ? -1.0f : 1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        int i = 0;
        for (float tick : xTicks) {
            float px = rect.x + axes.dataToFraction({tick, 0.0f}).x * rect.width;
            if (px < rect.x || px > rect.x + rect.width) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, scale);
            // Horizontal center at px; label sits outside the tick mark.
            float x = px - m.width * 0.5f;
            // bottom: text top at tickEnd + spacing (y - ascent = top)
            // top: text bottom at tickEnd - spacing (y+descent = bottom)
            float y = top ? tickEnd - kTickSpacing - m.height + m.ascent
                          : tickEnd + kTickSpacing + m.ascent;
            drawRichText(cmd, fullRect, label, x, y, labelColor, scale);
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
        // Minor tick labels (smaller, when a minor formatter is set).
        if (tc.minorFormatter) {
            auto minor = axisMinorTicks(tc, axes.xscale(), vp.x.min, vp.x.max,
                                        xTicks);
            tc.minorFormatter->setLocs(minor);
            float mScale = scale * 0.75f;
            int mi = 0;
            for (float t : minor) {
                float px = rect.x + axes.dataToFraction({t, 0.0f}).x * rect.width;
                if (px < rect.x || px > rect.x + rect.width) { ++mi; continue; }
                auto label = tc.minorFormatter->format(t, mi++);
                if (label.empty()) continue;
                auto m = measureRichText(label, mScale);
                float x = px - m.width * 0.5f;
                float y = tickEnd + kTickSpacing + m.ascent;
                drawRichText(cmd, fullRect, label, x, y, labelColor, mScale);
            }
        }
    }

    if (style.yAxis.visible) {
        const auto& tc = style.yAxis.ticks;
        auto yTicks = axisTicks(tc, axes.yscale(), vp.y.min, vp.y.max);
        plot::ScalarFormatter defaultFmt;
        plot::FormatStrFormatter strFmt("");
        plot::Formatter* fmt = axisFormatter(tc, yTicks, style,
                                             defaultFmt, strFmt);
        bool right = axes.yTicksRight();
        float edge = right ? float(rect.x + rect.width) : float(rect.x);
        float d = right ? 1.0f : -1.0f;
        float outLen = tc.majorSize * kPtToPx * (1.0f - tickInFrac(tc));
        float tickEnd = edge + d * outLen;
        int i = 0;
        for (float tick : yTicks) {
            float py = rect.y + rect.height -
                       axes.dataToFraction({0.0f, tick}).y * rect.height;
            if (py < rect.y || py > rect.y + rect.height) { ++i; continue; }
            auto label = tickLabel(tc, *fmt, tick, i++);
            if (label.empty()) continue;
            auto m = measureRichText(label, scale);
            // Label sits outside the tick mark, vertically centered at py.
            float x = right ? tickEnd + kTickSpacing
                            : tickEnd - kTickSpacing - m.width;
            float y = py + m.ascent - m.height * 0.5f;
            drawRichText(cmd, fullRect, label, x, y, labelColor, scale);
        }
        // Offset text above the top of the y axis.
        auto off = fmt->offsetText();
        if (!off.empty()) {
            auto m = measureRichText(off, scale * 0.8f);
            float x = right ? rect.x + rect.width - m.width : rect.x;
            float y = rect.y - kTickSpacing;
            drawRichText(cmd, fullRect, off, x, y, labelColor, scale * 0.8f);
        }
        if (tc.minorFormatter) {
            auto minor = axisMinorTicks(tc, axes.yscale(), vp.y.min, vp.y.max,
                                        yTicks);
            tc.minorFormatter->setLocs(minor);
            float mScale = scale * 0.75f;
            int mi = 0;
            for (float t : minor) {
                float py = rect.y + rect.height -
                           axes.dataToFraction({0.0f, t}).y * rect.height;
                if (py < rect.y || py > rect.y + rect.height) { ++mi; continue; }
                auto label = tc.minorFormatter->format(t, mi++);
                if (label.empty()) continue;
                auto m = measureRichText(label, mScale);
                float x = tickEnd - kTickSpacing - m.width;
                float y = py + m.ascent - m.height * 0.5f;
                drawRichText(cmd, fullRect, label, x, y, labelColor, mScale);
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

    // Draw the border rectangle around the axes area.
    // Use 2.0px width so MSAA produces full-coverage (pure black) pixels.
    // With 1.5px and 4x MSAA, a line centered at y=N covers pixels y=N-1 and
    // y=N at 75% coverage each, producing gray (64,64,64) instead of black.
    // With 2.0px, both pixels get 100% coverage → pure black.
    auto spineColor = style.xAxis.color;
    auto spineWidth = std::max(style.xAxis.lineWidth, 2.0f);
    const auto& sp = axes.spines();
    if (sp.left && sp.right && sp.bottom && sp.top) {
        spineRenderer_.drawRect(cmd, fullRect, rect, spineColor, spineWidth);
    } else {
        // Per-side spines (matplotlib spines[...].set_visible).
        float x0 = float(rect.x), y0 = float(rect.y);
        float x1 = x0 + float(rect.width), y1 = y0 + float(rect.height);
        auto side = [&](plot::Point2D a, plot::Point2D b) {
            plot::Point2D seg[2] = {a, b};
            spineRenderer_.drawLineStrip(cmd, fullRect, seg,
                                         spineColor, spineWidth);
        };
        if (sp.bottom) side({x0, y1}, {x1, y1});
        if (sp.top)    side({x0, y0}, {x1, y0});
        if (sp.left)   side({x0, y0}, {x0, y1});
        if (sp.right)  side({x1, y0}, {x1, y1});
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
        auto majors = axisTicks(tc, scale, lo, hi);
        auto majorFrac = toFrac(majors, yAxis);
        spineRenderer_.drawTicks(cmd, fullRect, rect, majorFrac,
                                 as.color, tc.majorSize * kPtToPx,
                                 yAxis, 0.0f, 1.0f, inF, farSide,
                                 std::max(tc.majorWidth * kPtToPx, 1.5f));
        auto minors = axisMinorTicks(tc, scale, lo, hi, majors);
        if (!minors.empty()) {
            auto minorFrac = toFrac(minors, yAxis);
            spineRenderer_.drawTicks(cmd, fullRect, rect, minorFrac,
                                     as.color, tc.minorSize * kPtToPx,
                                     yAxis, 0.0f, 1.0f, inF, farSide,
                                     std::max(tc.minorWidth * kPtToPx, 1.0f));
        }
    };
    if (style.xAxis.visible)
        drawAxisTicks(style.xAxis, axes.xscale(), vp.x.min, vp.x.max,
                      false, axes.xTicksTop());
    if (style.yAxis.visible)
        drawAxisTicks(style.yAxis, axes.yscale(), vp.y.min, vp.y.max,
                      true, axes.yTicksRight());
}

void Renderer::drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                          plot::Rect2D rect) {
    if (!spineInited_ || !textReady_) return;
    const auto& style = axes.style();
    const auto& lg = style.legend;
    if (!lg.visible) return;

    // Collect legend entries (label + color + marker) from all plot layers.
    struct LegendEntry { std::string label; plot::Color color; plot::LegendMarker marker; };
    std::vector<LegendEntry> entries;
    for (auto& plot : axes.plots()) {
        auto lbl = plot->label();
        if (lbl.empty()) continue;
        entries.push_back({std::move(lbl), plot->legendColor(), plot->legendMarker()});
    }
    if (entries.empty()) return;

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Font scale: 12pt maps to the 16px reference size (matplotlib's
    // "medium" legend fontsize ≈ font.size ≈ 10-12pt).
    const float scale = lg.font.size / 12.0f;
    const float fontPx = 16.0f * scale;
    const float rowHeight = textRenderer_.lineHeight(scale);
    const float pad = lg.borderPad * fontPx + (lg.fancyBox ? 2.0f : 0.0f);
    const float handleW = lg.handleLength * fontPx;
    const float textGap = lg.handleTextPad * fontPx;
    const float colGap = lg.columnSpacing * fontPx;

    // Column-major split into columns (matplotlib: each column is a
    // contiguous run of `rows` entries).
    const int n = static_cast<int>(entries.size());
    const int rows = lg.nrows > 0 ? lg.nrows
                                  : (n + std::max(1, lg.ncols) - 1) /
                                        std::max(1, lg.ncols);
    const int cols = (n + rows - 1) / rows;

    // Per-column width = handle + text gap + widest label in the column.
    std::vector<float> colW(cols, 0.0f);
    for (int c = 0; c < cols; ++c) {
        float maxW = 0;
        for (int r = 0; r < rows; ++r) {
            int idx = c * rows + r;
            if (idx >= n) break;
            maxW = std::max(maxW, measureRichText(entries[idx].label, scale).width);
        }
        colW[c] = handleW + textGap + maxW;
    }
    float contentW = 0;
    for (float w : colW) contentW += w;
    contentW += colGap * (cols - 1);

    // Title row.
    float titleW = 0.0f, titleH = 0.0f;
    const float titleScale = lg.titleFont.size / 12.0f;
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, titleScale);
        titleW = m.width;
        titleH = m.height + 4.0f;
    }

    const float boxW = pad * 2 + std::max(contentW, titleW);
    const float boxH = pad * 2 + titleH + rows * rowHeight;

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
    const auto la = parseLoc(lg.location);

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

    const float boxX = px - la.bx * boxW;
    const float boxY = py - (1.0f - la.by) * boxH;
    const plot::Rect2D boxRect{boxX, boxY, boxW, boxH};

    // Drop shadow behind the box.
    if (lg.shadow) {
        const float so = fontPx * 0.25f;
        spineRenderer_.drawFilledRect(cmd, fullRect,
            {boxX + so, boxY + so, boxW, boxH},
            plot::Color::fromRgba8(0, 0, 0, 100));
    }

    if (lg.frameOn) {
        // Semi-transparent background.
        auto bg = lg.faceColor;
        bg.a *= lg.frameAlpha;
        spineRenderer_.drawFilledRect(cmd, fullRect, boxRect, bg);
        spineRenderer_.drawRect(cmd, fullRect, boxRect, lg.edgeColor, 1.0f);
    }

    // Title (centered across the box).
    float contentTop = boxY + pad;
    if (!lg.title.empty()) {
        auto m = measureRichText(lg.title, titleScale);
        drawRichText(cmd, fullRect, lg.title,
                     boxX + boxW / 2.0f - m.width / 2.0f,
                     contentTop + m.ascent, style.textColor, titleScale);
        contentTop += titleH;
    }

    const auto labelColor = lg.labelColor.value_or(style.textColor);

    // Draw each entry: handle (line/marker) + text label.
    for (int i = 0; i < n; ++i) {
        const int c = i / rows, r = i % rows;
        float colX = boxX + pad;
        for (int j = 0; j < c; ++j) colX += colW[j] + colGap;
        const float y = contentTop + r * rowHeight;
        const float markerSize = fontPx;
        const float midY = y + rowHeight / 2.0f;
        const auto& e = entries[i];

        if (e.marker == plot::LegendMarker::Line) {
            plot::Point2D pts[] = {{colX, midY}, {colX + handleW, midY}};
            spineRenderer_.drawLineStrip(cmd, fullRect, pts, e.color, 2.0f);
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
            spineRenderer_.drawFilledRect(cmd, fullRect,
                {int32_t(sx), int32_t(midY - markerSize / 2.0f),
                 uint32_t(markerSize), uint32_t(markerSize)},
                e.color);
        }

        // Label text: baseline at top + ascent.
        auto m = measureRichText(e.label, scale);
        drawRichText(cmd, fullRect, e.label,
                     colX + handleW + textGap, y + m.ascent,
                     labelColor, scale);
    }
}

void Renderer::renderFrame(plot::Figure& figure) {
    auto ext = backend_.extent();
    figure.layout(plot::Extent2D{ext.width, ext.height});

    auto cmd = backend_.beginFrame();
    textRenderer_.resetScratch();
    spineRenderer_.resetScratch();
    for (auto& p : figure.placements()) {
        plot::Rect2D rect = p.axes->rect;
        vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}};

        // Draw grid background for this axes (per-axis enable).
        if (gridInited_ &&
            (p.axes->style().xAxis.grid || p.axes->style().yAxis.grid)) {
            gridRenderer_.draw(cmd, vrect, p.axes->transform(),
                               p.axes->style().xAxis,
                               p.axes->style().yAxis);
        }

        // Draw all plot layers in zorder (matplotlib zorder compositing).
        for (auto* plot : p.axes->drawOrder()) {
            const_cast<plot::IPlot*>(plot)->draw(cmd, *this, *p.axes, rect);
        }

        // Draw axis spines and tick marks.
        drawSpines(cmd, *p.axes, rect);

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

        // Draw colorbar (if enabled).
        drawColorbar(cmd, *p.axes, rect);
    }

    // Interactive overlays (§11): widgets + nav zoom rubber-band.
    if (spineInited_) {
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        Painter painter{cmd, *this, fullRect};
        for (auto& w : figure.widgets()) w->draw(painter);
        if (figure.navCreated() && figure.nav().hasZoomRect()) {
            auto [a, b] = figure.nav().zoomRect();
            plot::Rect2D zr{std::min(a.x, b.x), std::min(a.y, b.y),
                            std::abs(b.x - a.x), std::abs(b.y - a.y)};
            spineRenderer_.drawRect(cmd, fullRect, zr,
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

    // Find the value range from the first plot that has one (e.g., heatmap/surface).
    // For now, use the z-axis viewport if available, otherwise skip.
    float valueMin = 0.0f, valueMax = 1.0f;
    bool hasRange = false;
    const auto& vp = axes.viewport();
    if (vp.z.span() > 0) {
        valueMin = vp.z.min;
        valueMax = vp.z.max;
        hasRange = true;
    }
    if (!hasRange) return;

    auto ext = backend_.extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Layout: vertical color strip to the right of the axes.
    float stripX = rect.x + rect.width + style.colorbar.padding;
    float stripY = rect.y;
    float stripW = style.colorbar.width;
    float stripH = rect.height;
    const auto& cbs = style.colorbar;
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

    // Draw the color strip as a series of horizontal segments.
    uint32_t segments = 64;
    float segH = bodyH / segments;
    for (uint32_t i = 0; i < segments; ++i) {
        float t = float(i) / float(segments - 1);
        auto color = sampleAt(t);
        float y = bodyY0 + i * segH;
        spineRenderer_.drawFilledRect(cmd, fullRect,
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
    spineRenderer_.drawRect(cmd, fullRect,
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
        float scale = t.fontSize;
        auto m = measureRichText(t.text, scale);
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
            spineRenderer_.drawFilledRect(cmd, clipRect, bbox, t.bboxFaceColor);
            if (t.bboxEdgeColor.a > 0.0f) {
                spineRenderer_.drawRect(cmd, clipRect, bbox, t.bboxEdgeColor, 1.0f);
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
        vk::Rect2D clipRect = a.clipOn
            ? vk::Rect2D{vk::Offset2D{rect.x, rect.y},
                         vk::Extent2D{rect.width, rect.height}}
            : fullRect;

        // Draw arrow if requested.
        if (a.arrowStyle != plot::ArrowStyle::None) {
            // Sample the connection path (arc3/angle/arc/bar).
            auto path = plot::connectionPath(textPos, dataPos, a.connection,
                                             a.shrinkA, a.shrinkB);
            if (path.size() >= 2) {
                spineRenderer_.drawLineStrip(cmd, clipRect,
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
                        std::span{head1, 2}, a.arrowColor, a.arrowWidth);
                    spineRenderer_.drawLineStrip(cmd, clipRect,
                        std::span{head2, 2}, a.arrowColor, a.arrowWidth);
                }
            }
        }

        // Draw the text label.
        if (!a.text.empty()) {
            float scale = a.fontSize;
            auto m = measureRichText(a.text, scale);

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
                spineRenderer_.drawFilledRect(cmd, clipRect, bbox, a.bboxFaceColor);
                if (a.bboxEdgeColor.a > 0.0f) {
                    spineRenderer_.drawRect(cmd, clipRect, bbox, a.bboxEdgeColor, 1.0f);
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
    // Transparent output → clear with alpha 0 (matplotlib
    /// savefig(transparent=True)).
    if (options.transparent)
        backend_.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    prepare(figure);
    renderFrame(figure);

    // Restore the opaque clear for subsequent frames.
    if (options.transparent)
        backend_.setClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    auto pixels = backend_.readbackRgba8();
    if (pixels.empty()) return false;
    auto ext = backend_.extent();
    return encode::saveImage(pixels, ext.width, ext.height, path, options);
}

bool Renderer::saveAnimation(plot::Animation& anim,
                             const std::filesystem::path& path, double fps,
                             std::string_view writerName) {
    auto w = encode::createMovieWriter(writerName, path);
    if (!w) return false;
    auto ext = backend_.extent();
    if (!w->open(path, ext.width, ext.height, fps)) return false;
    size_t n = anim.frameCount();
    for (size_t i = 0; i < n; ++i) {
        anim.drawFrame(i);
        prepare(anim.figure());
        renderFrame(anim.figure());
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
    for (size_t i = 0; i < n; ++i) {
        anim.drawFrame(i);
        prepare(anim.figure());
        renderFrame(anim.figure());
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

} // namespace volcano::render
