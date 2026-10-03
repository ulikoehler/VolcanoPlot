// volcano/render/primitives/PointRenderer.hpp — scatter point renderer
//
// Backend-neutral interface. Vulkan impl: PointRendererVk in
// src/render/primitives/PointRenderer.cpp; WebGPU: op recorder in
// src/web/. Instances come from GpuServices::createPointRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>
#include <volcano/render/Offload.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace volcano::render::primitives {

/// Renders scatter points with per-marker SDF shading.
/// Supports the matplotlib marker set (polygons, stars, tripods, carets,
/// ticks) plus fill styles via `MarkerParams`.
struct MarkerParams {
    float code = 1.0f;      ///< plot::MarkerStyle value
    float fill = 0.0f;      ///< plot::MarkerFill value
    float numsides = 5.0f;  ///< for Polygon/StarN/AsteriskN/CircledN
    float angle = 0.0f;     ///< marker rotation, radians
};

class PointRenderer {
public:
    virtual ~PointRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> points,
                        std::span<const plot::Color> colors,
                        std::span<const float> sizes) = 0;
    /// Raw 3-D point centers + row-major view-projection; projected in
    /// the vertex shader when the `projection3d` offload switch allows
    /// and the backend implements upload3DDevice, else projected on the
    /// CPU. Sizes/colors are already screen-space attributes and pass
    /// through untouched.
    void upload3D(std::span<const plot::Point3D> points,
                  std::span<const plot::Color> colors,
                  std::span<const float> sizes,
                  const std::array<float, 16>& vp) {
        if (OffloadConfig::allowGpu(
                OffloadConfig::global().projection3d) &&
            upload3DDevice(points, colors, sizes, vp)) return;
        cpuPos_.clear();
        cpuPos_.reserve(points.size());
        for (const auto& p : points)
            cpuPos_.push_back(plot::projectPoint3D(vp, p));
        upload(cpuPos_, colors, sizes);
    }
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform,
                      uint32_t pointCount, MarkerParams marker = {}) const = 0;
    /// Same as draw() but with a scissor rect distinct from the viewport
    /// (e.g. canvas-space viewport + axes clip).
    virtual void draw(Cmd& cmd, plot::Rect2D viewport, plot::Rect2D scissor,
                      const plot::Transform2D& transform,
                      uint32_t pointCount, MarkerParams marker = {}) const = 0;

    /// Opaque token for the uploaded point buffer (vec2 data), for GPU
    /// autoscale / compute consumption. 0 when nothing is uploaded.
    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    /// Number of uploaded points (0 until upload() is called).
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;
    /// True once attribute buffers have been allocated (upload() or
    /// updatePoints()) — updatePoints() is safe to call only then.
    [[nodiscard]] virtual bool hasData() const noexcept = 0;
    /// In-place data update: overwrite the point buffer when the new
    /// count fits the existing allocation, else reallocate.
    /// Implementations must keep buffers referenced by recorded commands
    /// alive until the next resetScratch().
    virtual void updatePoints(std::span<const plot::Point2D> points,
                              std::span<const plot::Color> colors,
                              std::span<const float> sizes) = 0;
    /// Per-frame scratch release (retired buffers, pending state).
    virtual void resetScratch() = 0;

protected:
    /// Backend hook for upload3D (see LineSegmentRenderer).
    virtual bool upload3DDevice(std::span<const plot::Point3D>,
                                std::span<const plot::Color>,
                                std::span<const float>,
                                const std::array<float, 16>&) {
        return false;
    }

private:
    std::vector<plot::Point2D> cpuPos_;
};

} // namespace volcano::render::primitives
