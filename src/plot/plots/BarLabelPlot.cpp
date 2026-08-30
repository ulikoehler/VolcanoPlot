// volcano/plot/plots/BarLabelPlot.cpp — bar value labels implementation
#include "volcano/plot/plots/BarLabelPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace volcano::plot {

BarLabelPlot::BarLabelPlot(std::vector<float> x, std::vector<float> heights,
                           float baseline, BarLabelConfig config)
    : x_(std::move(x)), heights_(std::move(heights)),
      baseline_(baseline), config_(std::move(config)) {
    if (x_.size() != heights_.size())
        throw std::invalid_argument("BarLabelPlot: x and heights must have the same size");
}

std::string BarLabelPlot::formatValue(float v) const {
    char buf[64];
    if (config_.fmt.empty()) {
        // Default: "%.1g" — compact representation.
        std::snprintf(buf, sizeof(buf), "%.1g", v);
    } else {
        std::snprintf(buf, sizeof(buf), config_.fmt.c_str(), v);
    }
    return std::string(buf);
}

void BarLabelPlot::generateLabels() {
    if (!config_.labels.empty()) {
        generatedLabels_ = config_.labels;
        return;
    }
    generatedLabels_.clear();
    generatedLabels_.reserve(heights_.size());
    for (float h : heights_)
        generatedLabels_.push_back(formatValue(h));
}

Point2D BarLabelPlot::dataToPixel(const Viewport& v, const Rect2D& rect,
                                  float dx, float dy) const {
    float nx = (dx - v.x.min) / v.x.span();
    float ny = (dy - v.y.min) / v.y.span();
    float px = rect.x + nx * rect.width;
    float py = rect.y + (1.0f - ny) * rect.height;
    return {px, py};
}

void BarLabelPlot::prepare(render::Renderer& r) {
    generateLabels();
    prepared_ = true;
}

void BarLabelPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || generatedLabels_.empty()) return;

    auto& text = r.textRenderer();
    const auto& vp = axes.viewport();

    // Use full framebuffer as scissor so labels aren't clipped.
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    float fontSize = 16.0f * config_.fontScale;
    float charWidth = fontSize * 0.5f;

    for (size_t i = 0; i < x_.size() && i < generatedLabels_.size(); ++i) {
        const std::string& label = generatedLabels_[i];
        if (label.empty()) continue;

        float textWidth = label.size() * charWidth;
        float textHeight = fontSize;

        float px, py;
        if (config_.horizontal) {
            // Horizontal bars: labels to the right of bar end.
            float barEnd = baseline_ + heights_[i];
            auto p = dataToPixel(vp, rect, barEnd, x_[i]);
            if (config_.position == BarLabelPosition::Center) {
                px = p.x - textWidth * 0.5f;
                py = p.y - textHeight * 0.5f;
            } else {
                // Edge: outside the bar end.
                if (heights_[i] >= 0)
                    px = p.x + config_.padding;        // right of bar
                else
                    px = p.x - textWidth - config_.padding;  // left of bar
                py = p.y - textHeight * 0.5f;
            }
        } else {
            // Vertical bars: labels above/below bar top.
            float barTop = baseline_ + heights_[i];
            auto p = dataToPixel(vp, rect, x_[i], barTop);
            if (config_.position == BarLabelPosition::Center) {
                // Center of the bar.
                float barMid = baseline_ + heights_[i] * 0.5f;
                auto pm = dataToPixel(vp, rect, x_[i], barMid);
                px = pm.x - textWidth * 0.5f;
                py = pm.y - textHeight * 0.5f;
            } else {
                // Edge: outside the bar top.
                px = p.x - textWidth * 0.5f;
                if (heights_[i] >= 0)
                    py = p.y - textHeight - config_.padding;  // above bar
                else
                    py = p.y + config_.padding;               // below bar
            }
        }

        text.draw(cmd, fullRect, label, px, py, config_.color, config_.fontScale);
    }
}

void BarLabelPlot::contributeToAutoscale(Viewport& v) const {
    // Contribute bar positions and heights so labels are visible
    // even without a BarPlot in the same axes.
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv - 0.5f);
        v.x.max = std::max(v.x.max, xv + 0.5f);
    }
    v.y.min = std::min(v.y.min, baseline_);
    v.y.max = std::max(v.y.max, baseline_);
    for (float h : heights_) {
        v.y.min = std::min(v.y.min, baseline_ + h);
        v.y.max = std::max(v.y.max, baseline_ + h);
    }
}

} // namespace volcano::plot
