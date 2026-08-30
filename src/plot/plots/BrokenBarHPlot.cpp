// volcano/plot/plots/BrokenBarHPlot.cpp — broken horizontal bar chart implementation
#include "volcano/plot/plots/BrokenBarHPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <stdexcept>

namespace volcano::plot {

BrokenBarHPlot::BrokenBarHPlot(std::vector<BarHSegment> segments,
                               BrokenBarHConfig config)
    : segments_(std::move(segments)), config_(std::move(config)) {}

void BrokenBarHPlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& s = segments_[i];
        float x0 = s.xStart;
        float x1 = s.xStart + s.xWidth;
        float y0 = s.yStart;
        float y1 = s.yStart + s.yHeight;

        Point2D bl{x0, y0}, br{x1, y0}, tl{x0, y1}, tr{x1, y1};
        fillPositions_.push_back(bl);
        fillPositions_.push_back(br);
        fillPositions_.push_back(tl);
        fillPositions_.push_back(br);
        fillPositions_.push_back(tr);
        fillPositions_.push_back(tl);

        Color c = (i < config_.colors.size()) ? config_.colors[i] : config_.color;
        for (int k = 0; k < 6; ++k) fillColors_.push_back(c);
    }
}

void BrokenBarHPlot::prepare(render::Renderer& r) {
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

void BrokenBarHPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
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

void BrokenBarHPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& s : segments_) {
        v.x.min = std::min(v.x.min, s.xStart);
        v.x.max = std::max(v.x.max, s.xStart + s.xWidth);
        v.y.min = std::min(v.y.min, s.yStart);
        v.y.max = std::max(v.y.max, s.yStart + s.yHeight);
    }
}

} // namespace volcano::plot
