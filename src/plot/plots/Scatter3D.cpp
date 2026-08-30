// volcano/plot/plots/Scatter3D.cpp — 3D scatter plot implementation
#include "volcano/plot/plots/Scatter3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

Scatter3D::Scatter3D(std::vector<float> x, std::vector<float> y,
                     std::vector<float> z, Scatter3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("Scatter3D: x, y, z must have the same size");
}

Scatter3D::Scatter3D(std::vector<float> x, std::vector<float> y,
                     std::vector<float> z, std::vector<Color> colors,
                     Scatter3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      perPointColors_(std::move(colors)), config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("Scatter3D: x, y, z must have the same size");
    if (perPointColors_.size() != x_.size())
        throw std::invalid_argument("Scatter3D: colors size must match data size");
}

Scatter3D::Scatter3D(std::vector<float> x, std::vector<float> y,
                     std::vector<float> z, std::vector<Color> colors,
                     std::vector<float> sizes, Scatter3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      perPointColors_(std::move(colors)), perPointSizes_(std::move(sizes)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("Scatter3D: x, y, z must have the same size");
    if (perPointColors_.size() != x_.size())
        throw std::invalid_argument("Scatter3D: colors size must match data size");
    if (perPointSizes_.size() != x_.size())
        throw std::invalid_argument("Scatter3D: sizes size must match data size");
}

void Scatter3D::projectPoints() {
    projectedPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    if (x_.empty()) return;

    auto vp = camera_.viewProjection();

    for (size_t i = 0; i < x_.size(); ++i) {
        float px = x_[i], py = y_[i], pz = z_[i];

        float clipX = vp[0]*px + vp[1]*py + vp[2]*pz + vp[3];
        float clipY = vp[4]*px + vp[5]*py + vp[6]*pz + vp[7];
        float clipW = vp[12]*px + vp[13]*py + vp[14]*pz + vp[15];

        if (std::abs(clipW) < 1e-30f) continue;
        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;

        projectedPoints_.push_back({ndcX, ndcY});

        if (!perPointColors_.empty())
            markerColors_.push_back(perPointColors_[i]);
        else
            markerColors_.push_back(config_.color);

        if (!perPointSizes_.empty())
            markerSizes_.push_back(perPointSizes_[i]);
        else
            markerSizes_.push_back(config_.size);
    }
}

void Scatter3D::prepare(render::Renderer& r) {
    projectPoints();

    if (projectedPoints_.empty()) {
        prepared_ = true;
        return;
    }

    auto& ctx = r.backend().context();
    pointRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                        r.backend().sampleCount(), r.descriptorPool(),
                        r.pipelineCache());
    pointRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                          ctx.graphicsPool.handle(), ctx.allocator.handle(),
                          std::span{projectedPoints_}, std::span{markerColors_},
                          std::span{markerSizes_});
    prepared_ = true;
}

void Scatter3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                     const Axes& axes, Rect2D rect) {
    if (!prepared_ || projectedPoints_.empty()) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    pointRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(projectedPoints_.size()));
}

void Scatter3D::contributeToAutoscale(Viewport& v) const {
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
