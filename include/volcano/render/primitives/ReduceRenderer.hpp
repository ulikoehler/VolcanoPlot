// volcano/render/primitives/ReduceRenderer.hpp — GPU min/max reduction
//
// Backend-neutral interface (Vulkan impl: ReduceRendererVk).
// The shared instance comes from GpuServices::reduce().
#pragma once

#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <optional>

namespace volcano::render::primitives {

/// Result of a 2D min/max reduce: data-space bounding box.
struct MinMax2D {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

/// Parallel min/max reduce over a vec2 storage buffer (compute shader).
/// Synchronous on Vulkan; on the op-stream backend v1 returns nullopt
/// (callers fall back to CPU autoscale — the data already lives in the
/// WASM heap) and a mailbox-based variant may arrive next frame.
class ReduceRenderer {
public:
    virtual ~ReduceRenderer() = default;

    /// Compute the min/max bounding box of `pointBuffer` (vec2 data).
    virtual std::optional<MinMax2D> reduceMinMax2D(GpuBuf pointBuffer,
                                                 uint32_t count) = 0;

    [[nodiscard]] virtual bool ready() const noexcept = 0;
};

} // namespace volcano::render::primitives
