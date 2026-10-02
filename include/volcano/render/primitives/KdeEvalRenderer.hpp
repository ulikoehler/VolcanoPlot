// volcano/render/primitives/KdeEvalRenderer.hpp — GPU KDE evaluation
//
// Backend-neutral interface (Vulkan impl: KdeEvalRendererVk).
// The shared instance comes from GpuServices::kdeEval().
#pragma once

#include <volcano/plot/Types.hpp>

#include <cstdint>
#include <vector>

namespace volcano::render::primitives {

/// Evaluates a 2D kernel-density estimate over a regular grid from raw
/// samples (compute shader). Synchronous on Vulkan; returns an empty
/// vector on backends without compute — callers fall back to CPU KDE.
class KdeEvalRenderer {
public:
    virtual ~KdeEvalRenderer() = default;

    virtual std::vector<float> eval(const std::vector<plot::Point2D>& samples,
                                    uint32_t gridW, uint32_t gridH,
                                    float xMin, float xMax,
                                    float yMin, float yMax,
                                    float bwX, float bwY) = 0;

    [[nodiscard]] virtual bool ready() const noexcept = 0;
};

} // namespace volcano::render::primitives
