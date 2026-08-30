// volcano/plot/plots/Plot3D.cpp — 3D line plot implementation
#include "volcano/plot/plots/Plot3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

Plot3D::Plot3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
               Plot3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("Plot3D: x, y, z must have the same size");
}

void Plot3D::projectPoints() {
    projectedPoints_.clear();
    markerPoints_.clear();
    markerColors_.clear();

    if (x_.empty()) return;

    // Get the view-projection matrix (row-major 4x4).
    auto vp = camera_.viewProjection();

    for (size_t i = 0; i < x_.size(); ++i) {
        // Transform point to clip space: vp * [x, y, z, 1]^T
        float px = x_[i], py = y_[i], pz = z_[i];
        float pw = 1.0f;

        float clipX = vp[0]*px + vp[1]*py + vp[2]*pz + vp[3]*pw;
        float clipY = vp[4]*px + vp[5]*py + vp[6]*pz + vp[7]*pw;
        float clipZ = vp[8]*px + vp[9]*py + vp[10]*pz + vp[11]*pw;
        float clipW = vp[12]*px + vp[13]*py + vp[14]*pz + vp[15]*pw;

        // Perspective divide → NDC [-1, 1].
        if (std::abs(clipW) < 1e-30f) continue;
        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;
        (void)clipZ;  // depth not used for 2D line rendering

        projectedPoints_.push_back({ndcX, ndcY});

        if (config_.showMarkers) {
            markerPoints_.push_back({ndcX, ndcY});
            markerColors_.push_back(config_.markerColor);
        }
    }
}

void Plot3D::prepare(render::Renderer& r) {
    projectPoints();

    auto& ctx = r.backend().context();

    // Init and upload line data.
    lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (projectedPoints_.size() >= 2) {
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{projectedPoints_}, config_.color,
                             config_.lineWidth);
    }

    // Init and upload marker data.
    if (config_.showMarkers && !markerPoints_.empty()) {
        pointRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                            r.backend().sampleCount(), r.descriptorPool(),
                            r.pipelineCache());
        std::vector<float> sizes(markerPoints_.size(), config_.markerSize);
        pointRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{markerPoints_}, std::span{markerColors_},
                              std::span{sizes});
    }

    prepared_ = true;
}

void Plot3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                  const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    // Use a viewport that maps NDC [-1,1] directly (identity transform).
    // The projected points are already in NDC.
    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (projectedPoints_.size() >= 2)
        lineRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(projectedPoints_.size()));

    if (config_.showMarkers && !markerPoints_.empty())
        pointRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(markerPoints_.size()));
}

void Plot3D::contributeToAutoscale(Viewport& v) const {
    // 3D plots use their own camera projection, so autoscale doesn't
    // apply in the traditional 2D sense. However, we still contribute
    // the data ranges for the z-axis (used by colorbar etc.).
    for (size_t i = 0; i < x_.size(); ++i) {
        v.x.min = std::min(v.x.min, x_[i]);
        v.x.max = std::max(v.x.max, x_[i]);
        v.y.min = std::min(v.y.min, y_[i]);
        v.y.max = std::max(v.y.max, y_[i]);
        v.z.min = std::min(v.z.min, z_[i]);
        v.z.max = std::max(v.z.max, z_[i]);
    }
}

} // namespace volcano::plot
