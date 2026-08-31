// volcano/render/Renderer.cpp
#include "volcano/render/Renderer.hpp"
#include <volcano/plot/Colormap.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace volcano::render {

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

/// Get tick positions, using fixed positions if set, otherwise auto.
std::vector<float> getTickPositions(const plot::TickConfig& ticks,
                                    float vmin, float vmax) {
    if (ticks.positions && !ticks.positions->empty())
        return *ticks.positions;
    return autoTicks(vmin, vmax, ticks.nbins);
}

/// Get tick label for a position, using fixed labels if set, otherwise format.
std::string getTickLabel(const plot::TickConfig& ticks,
                         float pos, float step) {
    if (ticks.positions && ticks.labels &&
        ticks.positions->size() == ticks.labels->size()) {
        for (size_t i = 0; i < ticks.positions->size(); ++i) {
            if (std::abs((*ticks.positions)[i] - pos) < 1e-6f)
                return (*ticks.labels)[i];
        }
    }
    return formatTick(pos, step);
}

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
        auto m = textRenderer_.measureText(style.xAxis.label, scale);
        // Center horizontally at axes center, below the tick labels.
        // Top border at tickBottom + kTickSpacing + labelHeight + labelGap.
        constexpr float kXLabelGap = 8.0f;
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        float cy = float(rect.y + rect.height) + kTickLength + kTickSpacing +
                   16.0f + kXLabelGap + m.ascent;  // 16px approx label height
        textRenderer_.draw(cmd, fullRect,
            style.xAxis.label, cx, cy, labelColor, scale);
    }

    // --- Y axis label ---
    if (style.yAxis.visible && !style.yAxis.label.empty()) {
        // Rotate -90° (clockwise in screen space, Y-down) so the label
        // reads bottom-to-top. The rotation origin is the text baseline (x, y).
        // After rotation:
        //   - text width becomes vertical extent (upward from origin)
        //   - ascent becomes leftward extent, descent becomes rightward
        // We want: vertical center at axes middle, positioned left of tick labels.
        auto m = textRenderer_.measureText(style.yAxis.label, scale);
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
        textRenderer_.draw(cmd, fullRect,
            style.yAxis.label, ox, oy, labelColor, scale, kRotMinus90);
    }

    // --- Title ---
    if (!style.title.text.empty()) {
        auto m = textRenderer_.measureText(style.title.text, scale);
        float cx = rect.x + rect.width / 2.0f - m.width / 2.0f;
        // Position above the axes: text bottom at rect.y - padding.
        // text bottom = y + descent = y + (height - ascent)
        // So y = rect.y - pad - (height - ascent) = rect.y - pad - height + ascent
        constexpr float kTitlePad = 6.0f;
        float cy = float(rect.y) - kTitlePad - m.height + m.ascent;
        textRenderer_.draw(cmd, fullRect,
            style.title.text, cx, cy, style.title.color, scale);
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
        auto xTicks = getTickPositions(style.xAxis.ticks, vp.x.min, vp.x.max);
        float xStep = autoTickStep(vp.x.min, vp.x.max, style.xAxis.ticks.nbins);
        float tickBottom = float(rect.y + rect.height) + kTickLength;
        for (float tick : xTicks) {
            float px = rect.x + (tick - vp.x.min) / vp.x.span() * rect.width;
            if (px < rect.x || px > rect.x + rect.width) continue;
            auto label = getTickLabel(style.xAxis.ticks, tick, xStep);
            auto m = textRenderer_.measureText(label, scale);
            // Horizontal center at px: x = px - width/2
            // Top border at tickBottom + spacing: y - ascent = tickBottom + spacing
            float x = px - m.width * 0.5f;
            float y = tickBottom + kTickSpacing + m.ascent;
            textRenderer_.draw(cmd, fullRect, label, x, y, labelColor, scale);
        }
    }

    if (style.yAxis.visible) {
        auto yTicks = getTickPositions(style.yAxis.ticks, vp.y.min, vp.y.max);
        float yStep = autoTickStep(vp.y.min, vp.y.max, style.yAxis.ticks.nbins);
        float tickLeft = float(rect.x) - kTickLength;
        for (float tick : yTicks) {
            float py = rect.y + rect.height - (tick - vp.y.min) / vp.y.span() * rect.height;
            if (py < rect.y || py > rect.y + rect.height) continue;
            auto label = getTickLabel(style.yAxis.ticks, tick, yStep);
            auto m = textRenderer_.measureText(label, scale);
            // Right border at tickLeft - spacing: x + width = tickLeft - spacing
            // Vertical center at py: y - ascent + height/2 = py
            float x = tickLeft - kTickSpacing - m.width;
            float y = py + m.ascent - m.height * 0.5f;
            textRenderer_.draw(cmd, fullRect, label, x, y, labelColor, scale);
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
    spineRenderer_.drawRect(cmd, fullRect, rect, spineColor, spineWidth);

    // Draw tick marks. Use 2.0px width for the same MSAA reason.
    const auto& vp = axes.viewport();
    if (style.xAxis.visible) {
        auto xTicks = getTickPositions(style.xAxis.ticks, vp.x.min, vp.x.max);
        spineRenderer_.drawTicks(cmd, fullRect, rect, xTicks,
                                 style.xAxis.color, 4.0f,
                                 false, vp.x.min, vp.x.max);
    }
    if (style.yAxis.visible) {
        auto yTicks = getTickPositions(style.yAxis.ticks, vp.y.min, vp.y.max);
        spineRenderer_.drawTicks(cmd, fullRect, rect, yTicks,
                                 style.yAxis.color, 4.0f,
                                 true, vp.y.min, vp.y.max);
    }
}

void Renderer::drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                          plot::Rect2D rect) {
    if (!spineInited_ || !textReady_) return;
    const auto& style = axes.style();
    if (!style.legend.visible) return;

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

    // Layout: compute legend box size.
    float fontSize = 16.0f;
    float rowHeight = fontSize + 4.0f;
    float markerSize = fontSize;
    float textGap = 6.0f;
    float padding = 8.0f;

    // Compute max label width.
    float maxLabelWidth = 0;
    for (const auto& e : entries) {
        float w = e.label.size() * fontSize * 0.5f;
        if (w > maxLabelWidth) maxLabelWidth = w;
    }

    float boxW = padding * 2 + markerSize + textGap + maxLabelWidth;
    float boxH = padding * 2 + entries.size() * rowHeight;

    // Position: default to upper-right inside the axes rect.
    float boxX = rect.x + rect.width - boxW - 10.0f;
    float boxY = rect.y + 10.0f;

    // Draw semi-transparent background.
    auto bg = style.legend.faceColor;
    spineRenderer_.drawFilledRect(cmd, fullRect,
        {int32_t(boxX), int32_t(boxY), uint32_t(boxW), uint32_t(boxH)}, bg);

    // Draw border.
    spineRenderer_.drawRect(cmd, fullRect,
        {int32_t(boxX), int32_t(boxY), uint32_t(boxW), uint32_t(boxH)},
        style.legend.edgeColor, 1.0f);

    // Draw each entry: colored marker + text label.
    for (size_t i = 0; i < entries.size(); ++i) {
        float y = boxY + padding + i * rowHeight;
        float markerX = boxX + padding;
        // Draw the marker based on the plot type.
        if (entries[i].marker == plot::LegendMarker::Line) {
            // Horizontal line segment spanning the marker width.
            float midY = y + markerSize / 2.0f;
            plot::Point2D pts[] = {{markerX, midY}, {markerX + markerSize, midY}};
            spineRenderer_.drawLineStrip(cmd, fullRect, pts, entries[i].color, 2.0f);
        } else {
            // Filled square (default for Square and Circle markers).
            spineRenderer_.drawFilledRect(cmd, fullRect,
                {int32_t(markerX), int32_t(y), uint32_t(markerSize), uint32_t(markerSize)},
                entries[i].color);
        }
        // Draw the label text.
        float textX = markerX + markerSize + textGap;
        float textY = y + fontSize;  // baseline at bottom of marker
        textRenderer_.draw(cmd, fullRect, entries[i].label,
                           textX, textY, style.legend.edgeColor);
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

        // Draw grid background for this axes.
        if (gridInited_ && p.axes->style().xAxis.grid) {
            plot::Transform2D t;
            t.view = p.axes->viewport();
            t.logX = p.axes->logX();
            t.logY = p.axes->logY();
            gridRenderer_.draw(cmd, vrect, t,
                               p.axes->style().xAxis,
                               p.axes->style().yAxis);
        }

        // Draw all plot layers.
        for (auto& plot : p.axes->plots()) {
            plot->draw(cmd, *this, *p.axes, rect);
        }

        // Draw axis spines and tick marks.
        drawSpines(cmd, *p.axes, rect);

        // Draw text (axis labels, tick labels, title).
        if (textInited_ && textReady_) {
            drawText(cmd, *p.axes, rect);
        }

        // Draw legend (if enabled).
        drawLegend(cmd, *p.axes, rect);

        // Draw colorbar (if enabled).
        drawColorbar(cmd, *p.axes, rect);
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

    // Draw the color strip as a series of horizontal segments.
    // Each segment is a filled rect with a color from the colormap.
    const auto& cmap = plot::Colormap::byName(style.colorbar.colormap);
    uint32_t segments = 64;
    float segH = stripH / segments;
    for (uint32_t i = 0; i < segments; ++i) {
        float t = float(i) / float(segments - 1);
        auto color = cmap.sample(t);
        float y = stripY + i * segH;
        spineRenderer_.drawFilledRect(cmd, fullRect,
            {int32_t(stripX), int32_t(y), uint32_t(stripW), uint32_t(segH) + 1},
            color);
    }

    // Draw border around the strip.
    spineRenderer_.drawRect(cmd, fullRect,
        {int32_t(stripX), int32_t(stripY), uint32_t(stripW), uint32_t(stripH)},
        style.colorbar.edgeColor, 1.0f);

    // Draw tick labels.
    auto ticks = autoTicks(valueMin, valueMax, 8);
    float cbStep = autoTickStep(valueMin, valueMax, 8);
    for (float tick : ticks) {
        float t = (tick - valueMin) / (valueMax - valueMin);
        float y = stripY + (1.0f - t) * stripH;  // top = max, bottom = min
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

} // namespace volcano::render
