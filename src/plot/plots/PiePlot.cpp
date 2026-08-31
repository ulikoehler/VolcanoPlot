// volcano/plot/plots/PiePlot.cpp
#include "volcano/plot/plots/PiePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <cmath>
#include <format>
namespace volcano::plot {
void PiePlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void PiePlot::draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes&, Rect2D rect) {
    if (!prepared_) return;
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect);

    // Compute pie geometry in pixel coords.
    float cx = float(rect.x) + float(rect.width) * 0.5f;
    float cy = float(rect.y) + float(rect.height) * 0.5f;
    float halfW = float(rect.width) * 0.5f;
    float halfH = float(rect.height) * 0.5f;
    float radius = std::min(halfW, halfH) * 0.9f;

    constexpr float PI = 3.14159265358979323846f;

    float total = 0;
    for (auto v : data_.values) total += v;
    if (total <= 0) return;

    // Draw white edge lines between slices (matplotlib "wedgeprops edgecolor").
    // The shader flips Y, so screen_y = cy - sin(angle) * radius.
    auto& spine = r.spineRenderer();
    vk::Rect2D fullRect{{0, 0}, r.backend().extent()};
    float angle = 0.0f;
    for (size_t i = 0; i < data_.values.size(); ++i) {
        float sweep = 2.0f * PI * data_.values[i] / total;
        // Edge line at the start of each slice (from center to outer edge).
        float ex = cx + radius * std::cos(angle);
        float ey = cy - radius * std::sin(angle);  // Y flipped
        plot::Point2D edgePts[2] = {{cx, cy}, {ex, ey}};
        spine.drawLineStrip(cmd, fullRect, edgePts, Color::white(), 3.0f);
        angle += sweep;
    }

    // Draw labels (category labels outside, percentage labels inside).
    if (data_.labels.empty()) return;

    float fontSize = 14.0f;
    auto labelColor = Color::black();
    auto pctColor = Color::black();  // matplotlib uses black for percentage labels

    // matplotlib's pie() starts at 0° (3 o'clock) and goes counterclockwise.
    // With the shader's Y-flip, CCW on screen = increasing angle.
    angle = 0.0f;

    auto& text = r.textRenderer();

    for (size_t i = 0; i < data_.values.size() && i < data_.labels.size(); ++i) {
        float sweep = 2.0f * PI * data_.values[i] / total;
        float midAngle = angle + sweep * 0.5f;  // increasing = CCW on screen

        // Category label: outside the pie at 1.15 * radius.
        // Flip Y to match the shader's Y-flip (sin>0 = above on screen).
        float labelR = radius * 1.15f;
        float lx = cx + labelR * std::cos(midAngle);
        float ly = cy - labelR * std::sin(midAngle);  // Y flipped
        // Adjust for text width (approximate: charWidth = fontSize * 0.5)
        float tw = float(data_.labels[i].size()) * fontSize * 0.5f;
        // Position text so its center is at (lx, ly)
        float drawX = lx - tw * 0.5f;
        float drawY = ly - fontSize * 0.5f;
        text.draw(cmd, fullRect, data_.labels[i], drawX, drawY, labelColor, 1.0f);

        // Percentage label: inside the pie at 0.65 * radius.
        float pct = data_.values[i] / total * 100.0f;
        std::string pctStr = std::format("{:.0f}%", pct);
        float pctR = radius * 0.65f;
        float px = cx + pctR * std::cos(midAngle);
        float py = cy - pctR * std::sin(midAngle);  // Y flipped
        float pctW = float(pctStr.size()) * fontSize * 0.5f;
        text.draw(cmd, fullRect, pctStr,
                  px - pctW * 0.5f, py - fontSize * 0.5f, pctColor, 1.0f);

        angle += sweep;
    }
}
} // namespace volcano::plot
