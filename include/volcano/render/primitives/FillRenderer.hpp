// volcano/render/primitives/FillRenderer.hpp — filled polygon renderer
//
// Backend-neutral interface (Vulkan impl: FillRendererVk).
// Instances come from GpuServices::createFillRenderer().
#pragma once

#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <span>

namespace volcano::render::primitives {

/// Draws a filled polygon / triangle fan with per-vertex colors.
/// Used by fill_between, histograms, KDE areas, box bodies, etc.
class FillRenderer {
public:
    virtual ~FillRenderer() = default;

    virtual void upload(std::span<const plot::Point2D> positions,
                        std::span<const plot::Color> colors) = 0;

    /// Adopt buffers produced by another service (e.g. the pcolormesh
    /// tessellator). The resources are owned by GpuServices; the tokens
    /// stay valid for the services' lifetime.
    virtual void adoptBuffers(GpuBuf positions, GpuBuf colors,
                              uint32_t count) = 0;

    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform) const = 0;

    [[nodiscard]] virtual GpuBuf pointBuffer() const noexcept = 0;
    [[nodiscard]] virtual uint32_t pointCount() const noexcept = 0;
};

} // namespace volcano::render::primitives
