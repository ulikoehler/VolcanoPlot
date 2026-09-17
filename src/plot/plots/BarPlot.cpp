// volcano/plot/plots/BarPlot.cpp
#include "volcano/plot/plots/BarPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/backend/Backend.hpp"
#include "../VectorEmitHelpers.hpp"
namespace volcano::plot {
void BarPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    renderer_.init(ctx.device.handle(), r.backend().renderPass(), r.backend().sampleCount(), r.pipelineCache());
    renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(), ctx.graphicsPool.handle(),
                     ctx.allocator.handle(), data_);
    prepared_ = true;
}
void BarPlot::draw(vk::CommandBuffer cmd, render::Renderer&, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y}, vk::Extent2D{rect.width, rect.height}};
    renderer_.draw(cmd, vrect, t);
}
void BarPlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                         Rect2D rect) {
    float n = static_cast<float>(data_.heights.size());
    if (n <= 0) return;
    float bw = data_.width / n;
    auto toPx = pxMapper(axes, rect);
    for (size_t i = 0; i < data_.heights.size(); ++i) {
        float h = data_.heights[i];
        Point2D a, b;
        if (data_.horizontal) {
            float y0 = float(i) * bw + (1.0f - data_.width) * 0.5f;
            a = toPx({0.0f, y0 + bw}); b = toPx({h, y0});
        } else {
            float x0 = float(i) * bw + (1.0f - data_.width) * 0.5f;
            a = toPx({x0, 0.0f}); b = toPx({x0 + bw, h});
        }
        Color col = i < data_.colors.size() ? data_.colors[i]
                                            : Color::fromRgba8(31,119,180);
        Point2D q[4] = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
        c.polygon(q, col);
    }
}
void BarPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, 0.0f); v.x.max = std::max(v.x.max, float(data_.heights.size()));
    v.y.min = std::min(v.y.min, 0.0f);
    for (float h : data_.heights) v.y.max = std::max(v.y.max, h);
}
} // namespace volcano::plot
