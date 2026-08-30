// volcano/render/Grid3DRenderer.hpp — fwidth-based 3D dynamic grid
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/plot/Transform.hpp>
#include <vulkan/vulkan.hpp>

namespace volcano::core { class PipelineCache; }

namespace volcano::render {

/// Configuration for the 3D grid.
struct Grid3DStyle {
    /// Grid line color.
    plot::Color color = plot::Color::fromRgba8(200, 200, 200, 150);
    /// Whether to draw the X-Z floor grid (constant Y).
    bool floorXZ = true;
    /// Whether to draw the X-Y back wall grid (constant Z).
    bool backWallXY = true;
    /// Whether to draw the Y-Z side wall grid (constant X).
    bool sideWallYZ = true;
    /// Grid line step (world units). If <= 0, auto-computed.
    float step = 0.0f;
};

/// Renders a 3D dynamic grid using screen-space derivatives (fwidth).
///
/// The grid is drawn on the floor (X-Z plane) and optionally on the back
/// and side walls of the 3D axes box. Grid lines are computed in the
/// fragment shader using fwidth for anti-aliasing, so they never quantize
/// under zoom — the same technique as the 2D GridRenderer.
///
/// The renderer uses a fullscreen triangle and ray-casts into the 3D
/// scene to find the intersection with the floor/wall planes, then
/// computes grid line distances in world space.
class Grid3DRenderer {
public:
    void init(vk::Device device, vk::RenderPass renderPass,
              vk::SampleCountFlagBits samples, core::PipelineCache& cache,
              VmaAllocator allocator, vk::Queue queue, vk::CommandPool pool);

    /// Draw the 3D grid for the given camera and viewport.
    /// `rect` is the pixel rectangle of the axes.
    /// `viewport` is the data-space viewport (x, y, z ranges).
    /// `camera` is the 3D camera.
    /// `style` controls which planes get grids and grid appearance.
    void draw(vk::CommandBuffer cmd, vk::Rect2D rect,
              const plot::Viewport& viewport,
              const plot::Camera3D& camera,
              const Grid3DStyle& style) const;

private:
    vk::Device device_ = VK_NULL_HANDLE;
    core::ShaderModule vert_;
    core::ShaderModule frag_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    core::Buffer fullscreenBuffer_;
    bool inited_ = false;
};

} // namespace volcano::render
