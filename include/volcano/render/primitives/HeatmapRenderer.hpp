// volcano/render/primitives/HeatmapRenderer.hpp — colormapped grid renderer
//
// Backend-neutral interface (Vulkan impl: HeatmapRendererVk).
// Instances come from GpuServices::createHeatmapRenderer().
#pragma once

#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

namespace volcano::plot { class Colormap; }

namespace volcano::render::primitives {

/// Draws a 2D grid as a colormapped quad (imshow/pcolormesh/hist2d).
class HeatmapRenderer {
public:
    virtual ~HeatmapRenderer() = default;

    virtual void upload(const plot::Grid2D& grid, const plot::Colormap& cmap,
                        bool nanTransparent = false) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect,
                      const plot::Transform2D& transform) const = 0;
};

} // namespace volcano::render::primitives
