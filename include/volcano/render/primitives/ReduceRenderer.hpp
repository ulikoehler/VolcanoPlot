// volcano/render/primitives/ReduceRenderer.hpp — GPU-side parallel min/max reduce
//
// Computes the data-space bounding box (minX, maxX, minY, maxY) of a vec2
// storage buffer via a multi-pass compute shader reduce. Used for GPU
// autoscaling: instead of scanning point data on the CPU, the viewport is
// computed on the GPU with a workgroup-shared-memory parallel reduce.
//
// Algorithm:
//   * Workgroup size = 256. Each workgroup reduces up to 256 elements via
//     shared memory into one vec4 (minX, maxX, minY, maxY) written to an
//     intermediate buffer slot.
//   * Pass 1 reads the raw vec2 point buffer (reduceVec2 pipeline).
//   * Subsequent passes read vec4 partial results (reduceVec4 pipeline),
//     ping-ponging between two intermediate buffers until <= 256 partials
//     remain, then a final single-workgroup pass writes the result to a
//     host-visible output buffer.
//   * The dispatch is synchronous (submit + waitIdle) since autoscale runs
//     once during prepare(), not per frame.
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <optional>
#include <vector>

namespace volcano::render::primitives {

/// Result of a 2D min/max reduce: data-space bounding box.
struct MinMax2D {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

/// Parallel min/max reduce over a vec2 storage buffer (compute shader).
class ReduceRenderer {
public:
    ReduceRenderer() = default;

    /// Initialize the compute pipelines and buffers.
    /// `computeQueue`/`computePool` should be the compute family queue/pool.
    void init(vk::Device device, VmaAllocator allocator,
              vk::Queue computeQueue, vk::CommandPool computePool);

    /// Synchronously compute the min/max bounding box of `pointBuffer`
    /// (interpreted as `count` vec2 elements). Returns nullopt if `count`
    /// is 0 or the buffer is null — callers should fall back to CPU.
    std::optional<MinMax2D> reduceMinMax2D(vk::Buffer pointBuffer, uint32_t count);

    [[nodiscard]] bool ready() const noexcept { return inited_; }

private:
    /// Ensure the two ping-pong intermediate buffers can hold `slots` vec4s.
    void ensureIntermediateCapacity(uint32_t slots);

    /// Record one reduce pass into `cmd`: read `inBuf` (`inCount` elements),
    /// write per-workgroup partials to `outBuf`, or the final result to
    /// `outBuf[0]` when `isFinal`. `vec2Input` selects the pipeline. Uses
    /// `descSet` (a fresh set per pass — descriptor bindings cannot be
    /// re-updated for a set already recorded into a command buffer).
    void recordPass(vk::CommandBuffer cmd, vk::DescriptorSet descSet,
                    vk::Buffer inBuf, uint32_t inCount,
                    vk::Buffer outBuf, bool isFinal, bool vec2Input);

    vk::Device device_;
    vk::Queue computeQueue_;
    vk::CommandPool computePool_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    core::ShaderModule reduceVec2_;
    core::ShaderModule reduceVec4_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeVec2_;
    vk::UniquePipeline pipeVec4_;

    // Dedicated descriptor pool + a ring of pre-allocated descriptor sets
    // (one per reduce pass; max passes for any realistic point count is tiny).
    vk::UniqueDescriptorPool descPool_;
    std::vector<vk::DescriptorSet> descSets_;
    uint32_t descRingCap_ = 0;

    core::Buffer intermediateA_;
    core::Buffer intermediateB_;
    core::Buffer output_;        // host-visible, 1 vec4
    uint32_t interSlots_ = 0;    // current capacity (in vec4 slots)

    bool inited_ = false;
};

} // namespace volcano::render::primitives
