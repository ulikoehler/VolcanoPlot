// volcano/plot/plots/Errorbar3D.cpp — 3D error bar plot implementation
#include "volcano/plot/plots/Errorbar3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

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

void Errorbar3D::projectGeometry(float canvasW, float canvasH) {
    barSegs_.clear();
    capSegs_.clear();
    markerPoints_.clear();
    markerColors_.clear();
    markerSizes_.clear();

    if (x_.empty()) return;

    vp_ = camera_.viewProjection();
    const auto& vp = vp_;

    // Cap size in NDC: capSize is pixels; NDC spans [-1,1] = 2 units over
    // the canvas extent (mpl capsize is absolute points → absolute px).
    float capNdcX = config_.capSize * 2.0f / canvasW;
    float capNdcY = config_.capSize * 2.0f / canvasH;

    hasErrors_ = false;

    for (size_t i = 0; i < x_.size(); ++i) {
        float xlo, xhi, ylo, yhi, zlo, zhi;
        errBounds(i, config_.xerr, config_.xerrLower, config_.xerrUpper, xlo, xhi);
        errBounds(i, config_.yerr, config_.yerrLower, config_.yerrUpper, ylo, yhi);
        errBounds(i, config_.zerr, config_.zerrLower, config_.zerrUpper, zlo, zhi);

        float px = x_[i], py = y_[i], pz = z_[i];

        if (config_.drawMarker) {
            // Raw world-space marker center — projected by the renderer.
            markerPoints_.push_back({px, py, pz});
            markerColors_.push_back(config_.markerColor);
            markerSizes_.push_back(config_.markerSize);
        }

        // X error bar: from (px - xlo, py, pz) to (px + xhi, py, pz)
        if (xlo > 0 || xhi > 0) {
            hasErrors_ = true;
            barSegs_.push_back({px - xlo, py, pz});
            barSegs_.push_back({px + xhi, py, pz});

            if (config_.drawCaps) {
                // Caps perpendicular to the error bar direction in screen space.
                // For X error bars, caps are vertical in screen space (approximate).
                Point2D p1 = projectPoint3D(vp, {px - xlo, py, pz});
                Point2D p2 = projectPoint3D(vp, {px + xhi, py, pz});
                capSegs_.push_back({p1.x, p1.y - capNdcY});
                capSegs_.push_back({p1.x, p1.y + capNdcY});
                capSegs_.push_back({p2.x, p2.y - capNdcY});
                capSegs_.push_back({p2.x, p2.y + capNdcY});
            }
        }

        // Y error bar: from (px, py - ylo, pz) to (px, py + yhi, pz)
        if (ylo > 0 || yhi > 0) {
            hasErrors_ = true;
            barSegs_.push_back({px, py - ylo, pz});
            barSegs_.push_back({px, py + yhi, pz});

            if (config_.drawCaps) {
                Point2D p1 = projectPoint3D(vp, {px, py - ylo, pz});
                Point2D p2 = projectPoint3D(vp, {px, py + yhi, pz});
                capSegs_.push_back({p1.x - capNdcX, p1.y});
                capSegs_.push_back({p1.x + capNdcX, p1.y});
                capSegs_.push_back({p2.x - capNdcX, p2.y});
                capSegs_.push_back({p2.x + capNdcX, p2.y});
            }
        }

        // Z error bar: from (px, py, pz - zlo) to (px, py, pz + zhi)
        if (zlo > 0 || zhi > 0) {
            hasErrors_ = true;
            barSegs_.push_back({px, py, pz - zlo});
            barSegs_.push_back({px, py, pz + zhi});

            if (config_.drawCaps) {
                // Z error bars project roughly vertically, so caps are horizontal.
                Point2D p1 = projectPoint3D(vp, {px, py, pz - zlo});
                Point2D p2 = projectPoint3D(vp, {px, py, pz + zhi});
                capSegs_.push_back({p1.x - capNdcX, p1.y});
                capSegs_.push_back({p1.x + capNdcX, p1.y});
                capSegs_.push_back({p2.x - capNdcX, p2.y});
                capSegs_.push_back({p2.x + capNdcX, p2.y});
            }
        }
    }
}

void Errorbar3D::prepare(render::Renderer& r) {
    auto ext = r.gpu().extent();
    projectGeometry(float(ext.width), float(ext.height));


    // Init and upload error bar segments. Bars are world-space (GPU
    // projection); caps are fixed-NDC screen-space stubs.
    if (hasErrors_ && !barSegs_.empty()) {
        if (!errorRenderer_) errorRenderer_ = r.gpu().createLineSegmentRenderer();
        errorRenderer_->upload3D(std::span{barSegs_}, config_.errorbarColor,
                                 config_.errorbarWidth, vp_);
    }
    if (!capSegs_.empty()) {
        if (!capRenderer_) capRenderer_ = r.gpu().createLineSegmentRenderer();
        capRenderer_->upload(std::span{capSegs_}, config_.errorbarColor,
                             config_.errorbarWidth);
    }

    // Init and upload markers.
    if (config_.drawMarker && !markerPoints_.empty()) {
        if (!pointRenderer_) pointRenderer_ = r.gpu().createPointRenderer();
        pointRenderer_->upload3D(std::span{markerPoints_},
                                std::span{markerColors_},
                                std::span{markerSizes_}, vp_);
    }

    prepared_ = true;
}

void Errorbar3D::draw(render::Cmd& cmd, render::Renderer& r,
                      const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    Rect2D vrect = clipRectVk(rect, r.gpu().extent());

    if (hasErrors_ && !barSegs_.empty())
        errorRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(barSegs_.size()));
    if (!capSegs_.empty())
        capRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(capSegs_.size()));

    if (config_.drawMarker && !markerPoints_.empty())
        pointRenderer_->draw(cmd, vrect, t, static_cast<uint32_t>(markerPoints_.size()));
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
