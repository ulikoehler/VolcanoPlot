// volcano/plot/plots/Quiver3D.cpp — 3D vector field plot implementation
#include "volcano/plot/plots/Quiver3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

Point2D project3D(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0};
    return {clipX / clipW, clipY / clipW};
}

} // namespace

Quiver3D::Quiver3D(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, std::vector<float> u,
                   std::vector<float> v, std::vector<float> w,
                   Quiver3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      u_(std::move(u)), v_(std::move(v)), w_(std::move(w)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size() ||
        x_.size() != u_.size() || x_.size() != v_.size() || x_.size() != w_.size())
        throw std::invalid_argument("Quiver3D: all arrays must have the same size");
}

void Quiver3D::projectArrows() {
    shaftSegments_.clear();
    headPositions_.clear();
    headColors_.clear();

    if (x_.empty()) return;

    auto vp = camera_.viewProjection();
    float s = config_.scale;

    for (size_t i = 0; i < x_.size(); ++i) {
        float bx = x_[i], by = y_[i], bz = z_[i];
        float tx = bx + s * u_[i];
        float ty = by + s * v_[i];
        float tz = bz + s * w_[i];

        Point2D p0 = project3D(vp, bx, by, bz);
        Point2D p1 = project3D(vp, tx, ty, tz);

        // Shaft: line from base to tip.
        shaftSegments_.push_back(p0);
        shaftSegments_.push_back(p1);

        // Arrowhead: filled triangle at tip, perpendicular to shaft in screen space.
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 1e-6f) continue;  // degenerate arrow

        float ux = dx / len, uy = dy / len;
        // Perpendicular direction.
        float px = -uy, py = ux;

        // Arrowhead base corners.
        float hl = config_.headLength;
        float hw = config_.headWidth;
        Point2D h0 = {p1.x - ux * hl + px * hw, p1.y - uy * hl + py * hw};
        Point2D h1 = {p1.x - ux * hl - px * hw, p1.y - uy * hl - py * hw};

        // Triangle: tip + two base corners.
        headPositions_.push_back(p1);
        headPositions_.push_back(h0);
        headPositions_.push_back(h1);
        for (int k = 0; k < 3; ++k) headColors_.push_back(config_.color);
    }
}

void Quiver3D::prepare(render::Renderer& r) {
    projectArrows();

    auto& ctx = r.backend().context();

    shaftRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                        r.backend().sampleCount(), r.pipelineCache());
    if (!shaftSegments_.empty()) {
        shaftRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{shaftSegments_}, config_.color,
                              config_.lineWidth);
    }

    if (config_.filledHeads && !headPositions_.empty()) {
        headRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        headRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{headPositions_}, std::span{headColors_});
    }

    prepared_ = true;
}

void Quiver3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!shaftSegments_.empty())
        shaftRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(shaftSegments_.size()));

    if (config_.filledHeads && !headPositions_.empty())
        headRenderer_.draw(cmd, vrect, t);
}

void Quiver3D::contributeToAutoscale(Viewport& v) const {
    float s = config_.scale;
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i] + s * u_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i] + s * v_[i]);
        v.z.min = std::min(v.z.min, z_[i]);
        v.z.max = std::max(v.z.max, z_[i] + s * w_[i]);
    }
}

} // namespace volcano::plot
