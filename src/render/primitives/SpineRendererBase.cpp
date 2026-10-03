// volcano/render/primitives/SpineRendererBase.cpp — backend-neutral
// SpineRenderer members. Kept out of SpineRenderer.cpp (the Vulkan
// impl TU) so the op-stream web target can link the base class —
// fillRings is the key function, so this TU also emits the vtable
// and typeinfo every SpineRenderer subclass needs.
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/plot/Path.hpp"

namespace volcano::render::primitives {

void SpineRenderer::fillRings(Cmd& cmd, plot::Rect2D clip,
                              plot::Extent2D resolution,
                              std::span<const std::vector<plot::Point2D>> rings,
                              plot::Color face) {
    std::vector<plot::Point2D> tris;
    for (const auto& ring : rings) {
        auto t = plot::earClip(ring);
        tris.insert(tris.end(), t.begin(), t.end());
    }
    if (!tris.empty()) drawTriangles(cmd, clip, resolution, tris, face);
}

} // namespace volcano::render::primitives
