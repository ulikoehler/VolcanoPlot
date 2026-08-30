// volcano/plot/plots/StemPlot.cpp — stem plot implementation
#include "volcano/plot/plots/StemPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <stdexcept>

namespace volcano::plot {

StemPlot::StemPlot(std::vector<float> x, std::vector<float> y,
                   StemConfig config)
    : x_(std::move(x)), y_(std::move(y)), config_(std::move(config)) {
    if (x_.size() != y_.size())
        throw std::invalid_argument("StemPlot: x and y must have the same size");
}

StemPlot::StemPlot(std::vector<float> y, StemConfig config)
    : y_(std::move(y)), config_(std::move(config)) {
    x_.resize(y_.size());
    for (size_t i = 0; i < y_.size(); ++i) x_[i] = static_cast<float>(i);
}

void StemPlot::buildGeometry() {
    stemSegments_.clear();
    baselineSegments_.clear();
    markerPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    if (x_.empty()) return;

    // Build stem segments: vertical line from (x, baseline) to (x, y).
    for (size_t i = 0; i < x_.size(); ++i) {
        Point2D base{x_[i], config_.baseline};
        Point2D top{x_[i], y_[i]};
        stemSegments_.push_back(base);
        stemSegments_.push_back(top);

        if (config_.markers) {
            markerPoints_.push_back(top);
            markerColors_.push_back(config_.markerColor);
            markerSizes_.push_back(config_.markerSize);
        }
    }

    // Build baseline: horizontal line from (xMin, baseline) to (xMax, baseline).
    if (config_.showBaseline) {
        float xMin = *std::ranges::min_element(x_);
        float xMax = *std::ranges::max_element(x_);
        baselineSegments_.push_back({xMin, config_.baseline});
        baselineSegments_.push_back({xMax, config_.baseline});
    }
}

void StemPlot::prepare(render::Renderer& r) {
    buildGeometry();
    auto& ctx = r.backend().context();

    stemRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!stemSegments_.empty()) {
        stemRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{stemSegments_}, config_.lineColor,
                             config_.lineWidth);
    }

    if (config_.showBaseline && !baselineSegments_.empty()) {
        baselineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                               r.backend().sampleCount(), r.pipelineCache());
        baselineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                  ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                  std::span{baselineSegments_}, config_.baselineColor,
                                  config_.baselineWidth);
    }

    if (config_.markers && !markerPoints_.empty()) {
        markerRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                             r.backend().sampleCount(), r.descriptorPool(),
                             r.pipelineCache());
        markerRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                                ctx.graphicsPool.handle(), ctx.allocator.handle(),
                                std::span{markerPoints_}, std::span{markerColors_},
                                std::span{markerSizes_});
    }

    prepared_ = true;
}

void StemPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || x_.empty()) return;
    Transform2D t;
    t.view = axes.viewport();
    t.logX = axes.logX();
    t.logY = axes.logY();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (config_.showBaseline && !baselineSegments_.empty())
        baselineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(baselineSegments_.size()));

    stemRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(stemSegments_.size()));

    if (config_.markers && !markerPoints_.empty())
        markerRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(markerPoints_.size()));
}

void StemPlot::contributeToAutoscale(Viewport& v) const {
    for (float xv : x_) {
        v.x.min = std::min(v.x.min, xv);
        v.x.max = std::max(v.x.max, xv);
    }
    v.y.min = std::min(v.y.min, config_.baseline);
    v.y.max = std::max(v.y.max, config_.baseline);
    for (float yv : y_) {
        v.y.min = std::min(v.y.min, yv);
        v.y.max = std::max(v.y.max, yv);
    }
}

} // namespace volcano::plot
