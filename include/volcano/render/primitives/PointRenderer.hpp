// volcano/render/primitives/PointRenderer.hpp — scatter point renderer
//
// Backend-neutral interface. Vulkan impl: PointRendererVk in
// src/render/primitives/PointRenderer.cpp; WebGPU: op recorder in
// src/web/. Instances come from GpuServices::createPointRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

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
};

} // namespace volcano::render::primitives
