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
    // matplotlib's MaxNLocator uses rawStep = range / (nbins * 1.05)
    // to bias toward fewer ticks. We use range / nbins with rounding
    // thresholds that match matplotlib's 'auto' behavior.
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    float norm = rawStep / mag;
    float niceStep;
    // matplotlib thresholds: <1.5->1, <3->2, <6->5, else->10
    if (norm < 1.5f)       niceStep = 1.0f * mag;
    else if (norm < 3.0f)  niceStep = 2.0f * mag;
    else if (norm < 6.0f)  niceStep = 5.0f * mag;
    else                   niceStep = 10.0f * mag;

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
    float stepMag = std::abs(step);
    if (stepMag >= 1.0f) {
        // Integer steps: no decimal places.
        return std::format("{:.0f}", v);
    } else if (stepMag >= 0.1f) {
        // 0.1–0.9 steps: 1 decimal place.
        return std::format("{:.1f}", v);
    } else if (stepMag >= 0.01f) {
        // 0.01–0.09 steps: 2 decimal places.
        return std::format("{:.2f}", v);
    } else if (stepMag >= 0.001f) {
        // 0.001–0.009 steps: 3 decimal places.
        return std::format("{:.3f}", v);
    }
    return std::format("{:.1e}", v);
}

/// Compute the nice step size used by autoTicks.
float autoTickStep(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return 1.0f;
    float range = vmax - vmin;
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    float norm = rawStep / mag;
    if (norm < 1.5f)       return 1.0f * mag;
    else if (norm < 3.0f)  return 2.0f * mag;
    else if (norm < 6.0f)  return 5.0f * mag;
    else                   return 10.0f * mag;
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

    // --- X axis label ---
    if (style.xAxis.visible && !style.xAxis.label.empty()) {
        float textWidth = style.xAxis.label.size() * fontSize * 0.5f;
        float cx = rect.x + rect.width / 2.0f - textWidth / 2.0f;
        float cy = rect.y + rect.height + 25.0f;  // below the axes
        textRenderer_.draw(cmd, fullRect,
            style.xAxis.label, cx, cy, labelColor, scale);
    }

    // --- Y axis label ---
    if (style.yAxis.visible && !style.yAxis.label.empty()) {
        // Rotate 90° counterclockwise (in math convention) so the label
        // reads bottom-to-top. In screen space (Y-down), this is -90°
        // (i.e. -π/2 radians clockwise). The rotation origin is the text
        // baseline (x, y); we position it at the left-center of the axes.
        float textWidth = style.yAxis.label.size() * fontSize * 0.5f;
        float textHeight = fontSize;
        // Origin: left of the axes, vertically centered.
        // After rotation, the text extends upward from the origin.
        float ox = rect.x - textHeight - 10.0f;
        float oy = rect.y + rect.height / 2.0f + textWidth / 2.0f;
        constexpr float kRotMinus90 = -1.5707963267948966f; // -π/2
        textRenderer_.draw(cmd, fullRect,
            style.yAxis.label, ox, oy, labelColor, scale, kRotMinus90);
    }

    // --- Title ---
    if (!style.title.text.empty()) {
        float textWidth = style.title.text.size() * fontSize * 0.5f;
        float cx = rect.x + rect.width / 2.0f - textWidth / 2.0f;
        float cy = rect.y - 25.0f;  // above the axes
        textRenderer_.draw(cmd, fullRect,
            style.title.text, cx, cy, style.title.color, scale);
    }

    // --- Tick labels ---
    const auto& vp = axes.viewport();
    if (style.xAxis.visible) {
        auto xTicks = autoTicks(vp.x.min, vp.x.max, style.xAxis.ticks.nbins);
        float xStep = autoTickStep(vp.x.min, vp.x.max, style.xAxis.ticks.nbins);
        for (float tick : xTicks) {
            float px = rect.x + (tick - vp.x.min) / vp.x.span() * rect.width;
            if (px < rect.x || px > rect.x + rect.width) continue;
            auto label = formatTick(tick, xStep);
            float lw = label.size() * fontSize * 0.3f;
            textRenderer_.draw(cmd, fullRect,
                label, px - lw, rect.y + rect.height + 5.0f, labelColor, scale);
        }
    }

    if (style.yAxis.visible) {
        auto yTicks = autoTicks(vp.y.min, vp.y.max, style.yAxis.ticks.nbins);
        float yStep = autoTickStep(vp.y.min, vp.y.max, style.yAxis.ticks.nbins);
        for (float tick : yTicks) {
            float py = rect.y + rect.height - (tick - vp.y.min) / vp.y.span() * rect.height;
            if (py < rect.y || py > rect.y + rect.height) continue;
            auto label = formatTick(tick, yStep);
            float lw = label.size() * fontSize * 0.3f;
            textRenderer_.draw(cmd, fullRect,
                label, rect.x - lw - 5.0f, py, labelColor, scale);
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
    auto spineColor = style.xAxis.color;
    auto spineWidth = style.xAxis.lineWidth;
    spineRenderer_.drawRect(cmd, fullRect, rect, spineColor, spineWidth);

    // Draw tick marks.
    const auto& vp = axes.viewport();
    if (style.xAxis.visible) {
        auto xTicks = autoTicks(vp.x.min, vp.x.max, style.xAxis.ticks.nbins);
        spineRenderer_.drawTicks(cmd, fullRect, rect, xTicks,
                                 style.xAxis.color, 4.0f,
                                 false, vp.x.min, vp.x.max);
    }
    if (style.yAxis.visible) {
        auto yTicks = autoTicks(vp.y.min, vp.y.max, style.yAxis.ticks.nbins);
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
