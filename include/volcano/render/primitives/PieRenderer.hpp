// volcano/render/primitives/PieRenderer.hpp — pie chart renderer
//
// Backend-neutral interface (Vulkan impl: PieRendererVk).
// Instances come from GpuServices::createPieRenderer().
#pragma once

#include <volcano/plot/DataSeries.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/Cmd.hpp>

namespace volcano::render::primitives {

/// Draws pie/donut slices in the axes' pixel rect (pie coordinates are
/// handled by the viewport mapping — see the NDC fix in AGENTS.md).
class PieRenderer {
public:
    virtual ~PieRenderer() = default;

    virtual void upload(const plot::PieData& data) = 0;
    virtual void draw(Cmd& cmd, plot::Rect2D rect) const = 0;
};

} // namespace volcano::render::primitives
