// volcano/plot/plots/WireframePlot.cpp — 3D wireframe plot implementation
#include "volcano/plot/plots/WireframePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

Point2D project(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
}

} // namespace

WireframePlot::WireframePlot(Grid2D grid, WireframeConfig config)
    : grid_(std::move(grid)), config_(std::move(config)) {
    if (grid_.width == 0 || grid_.height == 0)
        throw std::invalid_argument("WireframePlot: grid dimensions must be non-zero");
    if (grid_.values.size() != grid_.width * grid_.height)
        throw std::invalid_argument("WireframePlot: values size must match width*height");
    if (config_.rowStride == 0) config_.rowStride = 1;
    if (config_.colStride == 0) config_.colStride = 1;
}

void WireframePlot::projectWireframe() {
    segments_.clear();

    if (grid_.width < 2 || grid_.height < 2) return;

    auto vp = camera_.viewProjection();

    // Map grid (i, j) to 3D coordinates.
    // x = xRange.min + i / (width-1) * xRange.span()
    // y = yRange.min + j / (height-1) * yRange.span()
    // z = values[j * width + i]
    float xSpan = grid_.xRange.span();
    float ySpan = grid_.yRange.span();
    if (xSpan == 0) xSpan = 1;
    if (ySpan == 0) ySpan = 1;

    auto gridToWorld = [&](uint32_t i, uint32_t j) -> Point3D {
        float x = grid_.xRange.min + static_cast<float>(i) / (grid_.width - 1) * xSpan;
        float y = grid_.yRange.min + static_cast<float>(j) / (grid_.height - 1) * ySpan;
        float z = grid_.values[static_cast<size_t>(j) * grid_.width + i];
        return {x, y, z};
    };

    // Row lines (constant j, varying i): connect adjacent points along x.
    for (uint32_t j = 0; j < grid_.height; j += config_.rowStride) {
        for (uint32_t i = 0; i + 1 < grid_.width; ++i) {
            Point3D p0 = gridToWorld(i, j);
            Point3D p1 = gridToWorld(i + 1, j);
            segments_.push_back(project(vp, p0.x, p0.y, p0.z));
            segments_.push_back(project(vp, p1.x, p1.y, p1.z));
        }
    }

    // Column lines (constant i, varying j): connect adjacent points along y.
    for (uint32_t i = 0; i < grid_.width; i += config_.colStride) {
        for (uint32_t j = 0; j + 1 < grid_.height; ++j) {
            Point3D p0 = gridToWorld(i, j);
            Point3D p1 = gridToWorld(i, j + 1);
            segments_.push_back(project(vp, p0.x, p0.y, p0.z));
            segments_.push_back(project(vp, p1.x, p1.y, p1.z));
        }
    }
}

void WireframePlot::prepare(render::Renderer& r) {
    projectWireframe();

    auto& ctx = r.backend().context();
    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!segments_.empty()) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{segments_}, config_.color, config_.lineWidth);
    }
    prepared_ = true;
}

void WireframePlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                         const Axes& axes, Rect2D rect) {
    if (!prepared_ || segments_.empty()) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(segments_.size()));
}

void WireframePlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, grid_.xRange.min);
    v.x.max = std::max(v.x.max, grid_.xRange.max);
    v.y.min = std::min(v.y.min, grid_.yRange.min);
    v.y.max = std::max(v.y.max, grid_.yRange.max);
    // z range from values
    for (float val : grid_.values) {
        if (std::isnan(val)) continue;
        v.z.min = std::min(v.z.min, val);
        v.z.max = std::max(v.z.max, val);
    }
}

} // namespace volcano::plot
