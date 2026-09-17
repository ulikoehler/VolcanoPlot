// volcano/render/primitives/EvalRenderer.hpp — GPU function evaluation
//
// Compiles a user GLSL expression `y = f(x)` into a compute shader that
// fills a vec2 output buffer over an x range. The output buffer uses
// vertex+storage usage so it can be bound directly as a line-strip
// vertex buffer — evaluation results never leave the GPU.
//
// Deep-zoom precision: the shader computes x = xBase + i*xStep where
// xBase is the (f64-computed) range minimum. For ranges far from the
// origin the caller may supply a `phase` split (PhaseDecomposer-style):
// the shader sees `x = xBase + xDelta` with both halves passed as f32
// push constants, keeping the delta small and precise.
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <string>

namespace volcano::render::primitives {

/// Compute-shader function evaluator for y = f(x) line plots.
class EvalRenderer {
public:
    EvalRenderer() = default;

    /// Initialize the shared pipeline machinery (descriptor pool, etc.).
    /// `computeQueue`/`computePool` should be the compute family queue/pool.
    void init(vk::Device device, VmaAllocator allocator,
              vk::Queue computeQueue, vk::CommandPool computePool);

    /// Compile `body` (a GLSL expression in terms of `x`, e.g.
    /// "sin(x*10.0)" or a full statement "y = sin(x); y *= x") into a
    /// compute pipeline. Returns false when compilation fails — callers
    /// should keep/fall back to a CPU evaluation.
    /// Pass `body` prefixed with "=" for expressions; otherwise treated
    /// as statements assigning `y`.
    bool compile(const std::string& body);

    /// Evaluate `count` samples over [xMin, xMax] into `out`
    /// (vec2 buffer with storage+vertex usage). Synchronous
    /// (submit + waitIdle) — runs during prepare(), not per frame.
    /// `xSplitBase`/`xSplitStep` implement phase decomposition: x is
    /// reconstructed as xSplitBase + i*xSplitStep in the shader.
    void eval(vk::Buffer out, double xMin, double xMax, uint32_t count);

    /// Allocate an output buffer for `count` vec2s (storage+vertex usage).
    [[nodiscard]] core::Buffer makeOutput(uint32_t count) const;

    [[nodiscard]] bool ready() const noexcept { return inited_; }
    [[nodiscard]] bool compiled() const noexcept { return bool(pipeline_); }

private:
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::Queue computeQueue_;
    vk::CommandPool computePool_;
    vk::UniqueDescriptorSetLayout descLayout_;
    vk::UniquePipelineLayout pipelineLayout_;
    vk::UniquePipeline pipeline_;
    vk::UniqueDescriptorPool descPool_;
    vk::DescriptorSet descSet_;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
