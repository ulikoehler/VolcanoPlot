// volcano/plot/plots/Errorbar3D.cpp — 3D error bar plot implementation
#include "volcano/plot/plots/Errorbar3D.hpp"
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

Errorbar3D::Errorbar3D(std::vector<float> x, std::vector<float> y,
                       std::vector<float> z, Errorbar3DConfig config)
    : x_(std::move(x)), y_(std::move(y)), z_(std::move(z)),
      config_(std::move(config)) {
    if (x_.size() != y_.size() || x_.size() != z_.size())
        throw std::invalid_argument("Errorbar3D: x, y, z must have the same size");
}

void Errorbar3D::errBounds(size_t i, const std::vector<float>& sym,
                           const std::vector<float>& lower,
                           const std::vector<float>& upper,
                           float& lo, float& hi) const {
    if (!lower.empty() && !upper.empty()) {
        lo = lower[i];
        hi = upper[i];
    } else if (!sym.empty()) {
        lo = sym[i];
        hi = sym[i];
    } else {
        lo = 0.0f;
        hi = 0.0f;
    }
}

void Errorbar3D::projectGeometry() {
    errorSegments_.clear();
    markerPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    if (x_.empty()) return;

    auto vp = camera_.viewProjection();

    // Cap size in NDC: convert pixel cap size to NDC (approximate).
    // NDC spans [-1, 1] = 2 units over the canvas. Cap is in pixels.
    // We use a fixed small NDC offset for caps.
    float capNdc = config_.capSize / 128.0f;  // approximate

    hasErrors_ = false;

    for (size_t i = 0; i < x_.size(); ++i) {
        float xlo, xhi, ylo, yhi, zlo, zhi;
        errBounds(i, config_.xerr, config_.xerrLower, config_.xerrUpper, xlo, xhi);
        errBounds(i, config_.yerr, config_.yerrLower, config_.yerrUpper, ylo, yhi);
        errBounds(i, config_.zerr, config_.zerrLower, config_.zerrUpper, zlo, zhi);

        float px = x_[i], py = y_[i], pz = z_[i];

        // Project the data point.
        Point2D center = project(vp, px, py, pz);

        if (config_.drawMarker) {
            markerPoints_.push_back(center);
            markerColors_.push_back(config_.markerColor);
            markerSizes_.push_back(config_.markerSize);
        }

        // X error bar: from (px - xlo, py, pz) to (px + xhi, py, pz)
        if (xlo > 0 || xhi > 0) {
            hasErrors_ = true;
            Point2D p1 = project(vp, px - xlo, py, pz);
            Point2D p2 = project(vp, px + xhi, py, pz);
            errorSegments_.push_back(p1);
            errorSegments_.push_back(p2);

            if (config_.drawCaps) {
                // Caps perpendicular to the error bar direction in screen space.
                // For X error bars, caps are vertical in screen space (approximate).
                errorSegments_.push_back({p1.x, p1.y - capNdc});
                errorSegments_.push_back({p1.x, p1.y + capNdc});
                errorSegments_.push_back({p2.x, p2.y - capNdc});
                errorSegments_.push_back({p2.x, p2.y + capNdc});
            }
        }

        // Y error bar: from (px, py - ylo, pz) to (px, py + yhi, pz)
        if (ylo > 0 || yhi > 0) {
            hasErrors_ = true;
            Point2D p1 = project(vp, px, py - ylo, pz);
            Point2D p2 = project(vp, px, py + yhi, pz);
            errorSegments_.push_back(p1);
            errorSegments_.push_back(p2);

            if (config_.drawCaps) {
                errorSegments_.push_back({p1.x - capNdc, p1.y});
                errorSegments_.push_back({p1.x + capNdc, p1.y});
                errorSegments_.push_back({p2.x - capNdc, p2.y});
                errorSegments_.push_back({p2.x + capNdc, p2.y});
            }
        }

        // Z error bar: from (px, py, pz - zlo) to (px, py, pz + zhi)
        if (zlo > 0 || zhi > 0) {
            hasErrors_ = true;
            Point2D p1 = project(vp, px, py, pz - zlo);
            Point2D p2 = project(vp, px, py, pz + zhi);
            errorSegments_.push_back(p1);
            errorSegments_.push_back(p2);

            if (config_.drawCaps) {
                // Z error bars project roughly vertically, so caps are horizontal.
                errorSegments_.push_back({p1.x - capNdc, p1.y});
                errorSegments_.push_back({p1.x + capNdc, p1.y});
                errorSegments_.push_back({p2.x - capNdc, p2.y});
                errorSegments_.push_back({p2.x + capNdc, p2.y});
            }
        }
    }
}

void Errorbar3D::prepare(render::Renderer& r) {
    projectGeometry();

    auto& ctx = r.backend().context();

    // Init and upload error bar segments.
    if (hasErrors_ && !errorSegments_.empty()) {
        errorRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                            r.backend().sampleCount(), r.pipelineCache());
        errorRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{errorSegments_}, config_.errorbarColor,
                              config_.errorbarWidth);
    }

    // Init and upload markers.
    if (config_.drawMarker && !markerPoints_.empty()) {
        pointRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                            r.backend().sampleCount(), r.descriptorPool(),
                            r.pipelineCache());
        pointRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                              ctx.graphicsPool.handle(), ctx.allocator.handle(),
                              std::span{markerPoints_}, std::span{markerColors_},
                              std::span{markerSizes_});
    }

    prepared_ = true;
}

void Errorbar3D::draw(vk::CommandBuffer cmd, render::Renderer&,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (hasErrors_ && !errorSegments_.empty())
        errorRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(errorSegments_.size()));

    if (config_.drawMarker && !markerPoints_.empty())
        pointRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(markerPoints_.size()));
}

void Errorbar3D::contributeToAutoscale(Viewport& v) const {
    for (size_t i = 0; i < x_.size(); ++i) {
        float xlo, xhi, ylo, yhi, zlo, zhi;
        errBounds(i, config_.xerr, config_.xerrLower, config_.xerrUpper, xlo, xhi);
        errBounds(i, config_.yerr, config_.yerrLower, config_.yerrUpper, ylo, yhi);
        errBounds(i, config_.zerr, config_.zerrLower, config_.zerrUpper, zlo, zhi);

        v.x.min = std::min(v.x.min, x_[i] - xlo);
        v.x.max = std::max(v.x.max, x_[i] + xhi);
        v.y.min = std::min(v.y.min, y_[i] - ylo);
        v.y.max = std::max(v.y.max, y_[i] + yhi);
        v.z.min = std::min(v.z.min, z_[i] - zlo);
        v.z.max = std::max(v.z.max, z_[i] + zhi);
    }
}

} // namespace volcano::plot
