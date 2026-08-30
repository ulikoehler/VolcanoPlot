// volcano/plot/plots/GroupedBarPlot.cpp — grouped bar chart implementation
#include "volcano/plot/plots/GroupedBarPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Default tab10 palette colors.
Color tab10(uint32_t i) {
    static const Color palette[] = {
        Color::fromRgba8(31, 119, 180, 255),
        Color::fromRgba8(255, 127, 14, 255),
        Color::fromRgba8(44, 160, 44, 255),
        Color::fromRgba8(214, 39, 40, 255),
        Color::fromRgba8(148, 103, 189, 255),
        Color::fromRgba8(140, 86, 75, 255),
        Color::fromRgba8(227, 119, 194, 255),
        Color::fromRgba8(127, 127, 127, 255),
        Color::fromRgba8(188, 189, 34, 255),
        Color::fromRgba8(23, 190, 207, 255),
    };
    return palette[i % 10];
}

} // namespace

GroupedBarPlot::GroupedBarPlot(std::vector<std::vector<float>> heights,
                               GroupedBarConfig config)
    : heights_(std::move(heights)), config_(std::move(config)) {
    if (heights_.empty())
        throw std::invalid_argument("GroupedBarPlot: at least one series required");
    nSeries_ = static_cast<uint32_t>(heights_.size());
    nGroups_ = static_cast<uint32_t>(heights_[0].size());
    for (uint32_t s = 0; s < nSeries_; ++s) {
        if (heights_[s].size() != nGroups_)
            throw std::invalid_argument("GroupedBarPlot: all series must have the same number of groups");
    }
}

Color GroupedBarPlot::seriesColor(uint32_t s) const {
    if (s < config_.colors.size()) return config_.colors[s];
    return tab10(s);
}

Color GroupedBarPlot::legendColor() const {
    return seriesColor(0);
}

void GroupedBarPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    // Each group occupies 1.0 unit on the category axis.
    // Group i is centered at x = i + 0.5.
    // Within a group, bars are placed side-by-side.
    // Total bar width per group = barWidth.
    // Each bar width = barWidth / nSeries.
    float barW = config_.barWidth / nSeries_;
    float groupStart = (1.0f - config_.barWidth) * 0.5f;

    for (uint32_t g = 0; g < nGroups_; ++g) {
        for (uint32_t s = 0; s < nSeries_; ++s) {
            float h = heights_[s][g];
            if (config_.horizontal) {
                // Horizontal: bars extend along x, categories on y.
                float y0 = g + groupStart + s * barW;
                float y1 = y0 + barW;
                float x0 = config_.baseline;
                float x1 = h;
                Point2D bl{x0, y0}, br{x1, y0}, ul{x0, y1}, ur{x1, y1};
                fillPositions_.push_back(bl);
                fillPositions_.push_back(br);
                fillPositions_.push_back(ul);
                fillPositions_.push_back(br);
                fillPositions_.push_back(ur);
                fillPositions_.push_back(ul);
            } else {
                // Vertical: bars extend along y, categories on x.
                float x0 = g + groupStart + s * barW;
                float x1 = x0 + barW;
                float y0 = config_.baseline;
                float y1 = h;
                Point2D bl{x0, y0}, br{x1, y0}, tl{x0, y1}, tr{x1, y1};
                fillPositions_.push_back(bl);
                fillPositions_.push_back(br);
                fillPositions_.push_back(tl);
                fillPositions_.push_back(br);
                fillPositions_.push_back(tr);
                fillPositions_.push_back(tl);
            }
            Color c = seriesColor(s);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(c);
        }
    }
}

void GroupedBarPlot::prepare(render::Renderer& r) {
    buildGeometry();
    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void GroupedBarPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    fillRenderer_.draw(cmd, vrect, t);
}

void GroupedBarPlot::contributeToAutoscale(Viewport& v) const {
    // X range: 0 to nGroups (categories).
    // Y range: baseline to max height (or min height if negative).
    if (config_.horizontal) {
        // Horizontal: x = heights, y = 0..nGroups
        for (const auto& series : heights_)
            for (float h : series) {
                v.x.min = std::min(v.x.min, h);
                v.x.max = std::max(v.x.max, h);
            }
        v.x.min = std::min(v.x.min, config_.baseline);
        v.x.max = std::max(v.x.max, config_.baseline);
        v.y.min = std::min(v.y.min, 0.0f);
        v.y.max = std::max(v.y.max, static_cast<float>(nGroups_));
    } else {
        // Vertical: x = 0..nGroups, y = heights
        v.x.min = std::min(v.x.min, 0.0f);
        v.x.max = std::max(v.x.max, static_cast<float>(nGroups_));
        v.y.min = std::min(v.y.min, config_.baseline);
        v.y.max = std::max(v.y.max, config_.baseline);
        for (const auto& series : heights_)
            for (float h : series) {
                v.y.min = std::min(v.y.min, h);
                v.y.max = std::max(v.y.max, h);
            }
    }
}

} // namespace volcano::plot
