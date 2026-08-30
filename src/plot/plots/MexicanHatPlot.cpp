// volcano/plot/plots/MexicanHatPlot.cpp — 3D Mexican hat wavelet plot
#include "volcano/plot/plots/MexicanHatPlot.hpp"
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

float projectDepth(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipZ = vp[8]*x + vp[9]*y + vp[10]*z + vp[11];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return 0.0f;
    return clipZ / clipW;
}

/// Evaluate the 2D Mexican hat (Ricker) wavelet.
float mexicanHat2D(float x, float y, float sigma) {
    float s2 = sigma * sigma;
    float r2 = x * x + y * y;
    float t = r2 / s2;
    return (2.0f - t) * std::exp(-t * 0.5f);
}

struct ProjectedQuad {
    Point2D v[4];
    float depth;
    float zAvg;
};

} // namespace

MexicanHatPlot::MexicanHatPlot(float sigma, Range xRange, Range yRange,
                               uint32_t gridW, uint32_t gridH,
                               MexicanHatConfig config)
    : sigma_(sigma), xRange_(xRange), yRange_(yRange),
      gridW_(gridW), gridH_(gridH), config_(std::move(config)) {
    if (sigma_ <= 0.0f) sigma_ = 1.0f;
    if (gridW_ < 2) gridW_ = 2;
    if (gridH_ < 2) gridH_ = 2;
}

void MexicanHatPlot::evaluateWavelet() {
    zValues_.resize(static_cast<size_t>(gridW_) * gridH_);
    zMin_ = std::numeric_limits<float>::max();
    zMax_ = std::numeric_limits<float>::lowest();

    for (uint32_t j = 0; j < gridH_; ++j) {
        float ty = float(j) / float(gridH_ - 1);
        float y = yRange_.min + ty * yRange_.span();
        for (uint32_t i = 0; i < gridW_; ++i) {
            float tx = float(i) / float(gridW_ - 1);
            float x = xRange_.min + tx * xRange_.span();
            float z = mexicanHat2D(x, y, sigma_);
            zValues_[j * gridW_ + i] = z;
            zMin_ = std::min(zMin_, z);
            zMax_ = std::max(zMax_, z);
        }
    }
}

void MexicanHatPlot::projectSurface() {
    fillPositions_.clear();
    fillColors_.clear();
    wireSegments_.clear();

    if (zValues_.empty()) return;

    auto vp = camera_.viewProjection();
    const auto& cmap = Colormap::byName(config_.colormap);

    // Helper to get (x, y, z) at grid point (i, j).
    auto gridPoint = [&](uint32_t i, uint32_t j) -> Point3D {
        float tx = float(i) / float(gridW_ - 1);
        float ty = float(j) / float(gridH_ - 1);
        return {
            xRange_.min + tx * xRange_.span(),
            yRange_.min + ty * yRange_.span(),
            zValues_[j * gridW_ + i]
        };
    };

    float zSpan = zMax_ - zMin_;
    if (zSpan < 1e-30f) zSpan = 1.0f;

    // Build quads: each quad is (i,j), (i+1,j), (i+1,j+1), (i,j+1).
    std::vector<ProjectedQuad> quads;
    quads.reserve(static_cast<size_t>(gridW_ - 1) * (gridH_ - 1));

    for (uint32_t j = 0; j < gridH_ - 1; ++j) {
        for (uint32_t i = 0; i < gridW_ - 1; ++i) {
            Point3D p00 = gridPoint(i, j);
            Point3D p10 = gridPoint(i + 1, j);
            Point3D p11 = gridPoint(i + 1, j + 1);
            Point3D p01 = gridPoint(i, j + 1);

            ProjectedQuad q;
            q.v[0] = project(vp, p00.x, p00.y, p00.z);
            q.v[1] = project(vp, p10.x, p10.y, p10.z);
            q.v[2] = project(vp, p11.x, p11.y, p11.z);
            q.v[3] = project(vp, p01.x, p01.y, p01.z);

            float avgDepth = 0.0f;
            avgDepth += projectDepth(vp, p00.x, p00.y, p00.z);
            avgDepth += projectDepth(vp, p10.x, p10.y, p10.z);
            avgDepth += projectDepth(vp, p11.x, p11.y, p11.z);
            avgDepth += projectDepth(vp, p01.x, p01.y, p01.z);
            q.depth = avgDepth * 0.25f;

            q.zAvg = (p00.z + p10.z + p11.z + p01.z) * 0.25f;

            quads.push_back(q);
        }
    }

    // Sort quads back-to-front (painter's algorithm).
    std::sort(quads.begin(), quads.end(),
              [](const ProjectedQuad& a, const ProjectedQuad& b) {
                  return a.depth > b.depth;
              });

    // Build fill triangles (2 per quad) with colormap colors.
    if (config_.drawSurface) {
        for (const auto& q : quads) {
            float t = (q.zAvg - zMin_) / zSpan;
            Color c = cmap.sample(t);

            // Triangle 1: v0, v1, v2
            fillPositions_.push_back(q.v[0]);
            fillPositions_.push_back(q.v[1]);
            fillPositions_.push_back(q.v[2]);
            // Triangle 2: v0, v2, v3
            fillPositions_.push_back(q.v[0]);
            fillPositions_.push_back(q.v[2]);
            fillPositions_.push_back(q.v[3]);
            for (int k = 0; k < 6; ++k) fillColors_.push_back(c);
        }
    }

    // Build wireframe segments.
    if (config_.drawWireframe) {
        uint32_t rStride = std::max(1u, config_.rowStride);
        uint32_t cStride = std::max(1u, config_.colStride);

        // Row lines (constant j, vary i).
        for (uint32_t j = 0; j < gridH_; j += rStride) {
            for (uint32_t i = 0; i + 1 < gridW_; ++i) {
                Point3D p0 = gridPoint(i, j);
                Point3D p1 = gridPoint(i + 1, j);
                wireSegments_.push_back(project(vp, p0.x, p0.y, p0.z));
                wireSegments_.push_back(project(vp, p1.x, p1.y, p1.z));
            }
        }

        // Column lines (constant i, vary j).
        for (uint32_t i = 0; i < gridW_; i += cStride) {
            for (uint32_t j = 0; j + 1 < gridH_; ++j) {
                Point3D p0 = gridPoint(i, j);
                Point3D p1 = gridPoint(i, j + 1);
                wireSegments_.push_back(project(vp, p0.x, p0.y, p0.z));
                wireSegments_.push_back(project(vp, p1.x, p1.y, p1.z));
            }
        }
    }
}

void MexicanHatPlot::prepare(render::Renderer& r) {
    evaluateWavelet();
    projectSurface();

    auto& ctx = r.backend().context();

    if (config_.drawSurface && !fillPositions_.empty()) {
        fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }

    if (config_.drawWireframe && !wireSegments_.empty()) {
        wireRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        wireRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{wireSegments_}, config_.wireframeColor,
                             config_.wireframeWidth);
    }

    prepared_ = true;
}

void MexicanHatPlot::draw(vk::CommandBuffer cmd, render::Renderer&,
                          const Axes& axes, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};

    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (config_.drawSurface && !fillPositions_.empty())
        fillRenderer_.draw(cmd, vrect, t);

    if (config_.drawWireframe && !wireSegments_.empty())
        wireRenderer_.draw(cmd, vrect, t, static_cast<uint32_t>(wireSegments_.size()));
}

void MexicanHatPlot::contributeToAutoscale(Viewport& v) const {
    v.x.min = std::min(v.x.min, xRange_.min);
    v.x.max = std::max(v.x.max, xRange_.max);
    v.y.min = std::min(v.y.min, yRange_.min);
    v.y.max = std::max(v.y.max, yRange_.max);
    // z range from evaluated wavelet (if already evaluated).
    if (!zValues_.empty()) {
        v.z.min = std::min(v.z.min, zMin_);
        v.z.max = std::max(v.z.max, zMax_);
    }
}

} // namespace volcano::plot
