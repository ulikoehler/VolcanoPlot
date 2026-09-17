// volcano/render/primitives/KdeEvalRenderer.hpp — GPU KDE evaluation
//
// Streams raw 2D samples to the GPU and evaluates a 2D Gaussian kernel
// density estimate into a W×H grid via compute shader (one thread per
// cell, gather over the sample buffer). The density grid lands in a
// host-visible buffer for the heatmap upload path.
#pragma once

#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <vector>

namespace volcano::plot { struct Point2D; }

namespace volcano::render::primitives {

/// Compute-shader 2D Gaussian KDE evaluator.
class KdeEvalRenderer {
public:
    KdeEvalRenderer() = default;

    /// Initialize the pipeline (fixed kernel — no per-call compile).
    void init(vk::Device device, VmaAllocator allocator,
              vk::Queue computeQueue, vk::CommandPool computePool);

    /// Evaluate the KDE of `samples` into a gridW×gridH grid covering
    /// [xMin,xMax]×[yMin,yMax] with per-axis bandwidths bwX/bwY.
    /// Returns the density values (row-major, gridW*gridH) or an empty
    /// vector on failure — callers fall back to CPU.
    std::vector<float> eval(const std::vector<plot::Point2D>& samples,
                            uint32_t gridW, uint32_t gridH,
                            float xMin, float xMax, float yMin, float yMax,
                            float bwX, float bwY);

    [[nodiscard]] bool ready() const noexcept { return inited_; }

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
    core::Buffer sampleBuf_;
    core::Buffer gridBuf_;
    uint32_t sampleCap_ = 0, gridCap_ = 0;
    bool inited_ = false;
};

} // namespace volcano::render::primitives
