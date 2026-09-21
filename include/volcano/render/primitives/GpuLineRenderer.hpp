// volcano/render/primitives/GpuLineRenderer.hpp — GPU polyline stroker
//
// Solid (non-dashed) polylines are stroked on the GPU: a compute shader
// reads pixel-space points and expands them into a LineVertex triangle
// soup (segment quads + miter/bevel/round join wedges + butt/square/
// round caps) in a device-local vertex buffer. The result is drawn with
// the spine fill pipeline via drawTrianglesGpu.
//
// This mirrors plot::strokePolyline (CPU) for the solid case; dashed
// lines stay on the CPU path (dash walks need sequential arc length).
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/plot/Stroke.hpp>
#include <volcano/plot/Types.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <optional>
#include <vector>

namespace volcano::core { class PipelineCache; class DescriptorPool; }

namespace volcano::render::primitives {

class GpuLineRenderer {
public:
    GpuLineRenderer() = default;
    void init(vk::Device device, VmaAllocator allocator,
              core::DescriptorPool& descPool,
              core::PipelineCache& cache);

    /// Per-frame reset of the bump allocators; frees buffers retired by
    /// mid-frame growth (previous frame is complete by then).
    void resetScratch();

    struct Mesh {
        vk::Buffer buffer;
        vk::DeviceSize firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    /// Record the tessellation dispatch on `cmd` (a pre-pass command
    /// buffer recorded outside/before the render pass). Returns the
    /// buffer+range to draw with SpineRenderer::drawTrianglesGpu.
    Mesh tessellate(vk::CommandBuffer cmd,
                    std::span<const plot::Point2D> px,
                    const plot::StrokeParams& sp,
                    plot::Color color);

    [[nodiscard]] bool inited() const noexcept { return inited_; }

private:
    static constexpr uint32_t kJoinVerts = 8 * 3;  // round fan: 8 tris
    static constexpr uint32_t kCapVerts = 8 * 3;   // semicircle fan
    static constexpr uint32_t kSegVerts = 6;       // segment quad
    // verts per input point: (n-1) quads in region A + n join/cap slots
    // in region B → total = kSegVerts*(n-1) + kJoinVerts*n.

    void ensureIn(size_t points);
    void ensureOut(size_t verts);
    void rebind();

    vk::Device device_;
    VmaAllocator allocator_ = nullptr;
    core::DescriptorPool* descPool_ = nullptr;
    bool inited_ = false;

    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipeLayout_;
    vk::UniquePipeline pipe_;
    vk::DescriptorSet dset_;

    core::Buffer inBuf_;   // host-visible storage: vec2 points
    core::Buffer outBuf_;  // device-local VertexStorage: LineVertex soup
    std::vector<core::Buffer> retiredIn_, retiredOut_;
    vk::DeviceSize inOff_ = 0, outOff_ = 0;   // bump offsets (elem units)
};

} // namespace volcano::render::primitives
