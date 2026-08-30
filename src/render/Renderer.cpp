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
std::vector<float> autoTicks(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return {};
    float range = vmax - vmin;
    float rawStep = range / nbins;
    // Round step to a "nice" number: 1, 2, 5 × 10^k.
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    float norm = rawStep / mag;
    float niceStep;
    if (norm < 1.5f)       niceStep = 1.0f * mag;
    else if (norm < 3.0f)  niceStep = 2.0f * mag;
    else if (norm < 7.0f)  niceStep = 5.0f * mag;
    else                   niceStep = 10.0f * mag;

    float start = std::ceil(vmin / niceStep) * niceStep;
    std::vector<float> ticks;
    for (float v = start; v <= vmax + niceStep * 0.001f; v += niceStep) {
        ticks.push_back(v);
    }
    return ticks;
}

/// Format a tick value as a short string.
std::string formatTick(float v) {
    if (std::abs(v) < 1e-10f) return "0";
    if (std::abs(v) >= 10000.0f || std::abs(v) < 0.001f) {
        return std::format("{:.1e}", v);
    }
    if (std::abs(v) >= 100.0f) return std::format("{:.0f}", v);
    if (std::abs(v) >= 10.0f)  return std::format("{:.1f}", v);
    return std::format("{:.2f}", v);
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
        for (float tick : xTicks) {
            float px = rect.x + (tick - vp.x.min) / vp.x.span() * rect.width;
            if (px < rect.x || px > rect.x + rect.width) continue;
            auto label = formatTick(tick);
            float lw = label.size() * fontSize * 0.3f;
            textRenderer_.draw(cmd, fullRect,
                label, px - lw, rect.y + rect.height + 5.0f, labelColor, scale);
        }
    }

    if (style.yAxis.visible) {
        auto yTicks = autoTicks(vp.y.min, vp.y.max, style.yAxis.ticks.nbins);
        for (float tick : yTicks) {
            float py = rect.y + rect.height - (tick - vp.y.min) / vp.y.span() * rect.height;
            if (py < rect.y || py > rect.y + rect.height) continue;
            auto label = formatTick(tick);
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

    // Collect legend entries (label + color) from all plot layers.
    struct LegendEntry { std::string label; plot::Color color; };
    std::vector<LegendEntry> entries;
    for (auto& plot : axes.plots()) {
        auto lbl = plot->label();
        if (lbl.empty()) continue;
        entries.push_back({std::move(lbl), plot->legendColor()});
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
        // Draw a filled square as the marker.
        spineRenderer_.drawFilledRect(cmd, fullRect,
            {int32_t(markerX), int32_t(y), uint32_t(markerSize), uint32_t(markerSize)},
            entries[i].color);
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
    for (float tick : ticks) {
        float t = (tick - valueMin) / (valueMax - valueMin);
        float y = stripY + (1.0f - t) * stripH;  // top = max, bottom = min
        // Draw tick mark.
        plot::Point2D tickPts[2] = {
            {stripX + stripW, y},
            {stripX + stripW + 4.0f, y},
        };
        // Draw label.
        std::string label = formatTick(tick);
        textRenderer_.draw(cmd, fullRect, label,
                           stripX + stripW + 8.0f, y + 6.0f,
                           style.colorbar.labelColor);
    }
}

} // namespace volcano::render
