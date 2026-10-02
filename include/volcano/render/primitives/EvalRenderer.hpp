// volcano/render/primitives/EvalRenderer.hpp — GPU function evaluation
//
// Backend-neutral interface (Vulkan impl: EvalRendererVk).
// Instances come from GpuServices::createEvalRenderer().
#pragma once

#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <string>

namespace volcano::render::primitives {

/// Evaluates y = f(x) into a point buffer via a compute shader — one
/// sample per output slot. Backs FunctionPlot/ChirpPlot ("infinite
/// zoom": sample count tracks canvas width, not viewport).
///
/// On the op-stream backend compile() translates the GLSL expression
/// body to WGSL (web/GlslExpr.cpp) and eval() emits an EvalFn op.
class EvalRenderer {
public:
    virtual ~EvalRenderer() = default;

    /// Compile the GLSL expression body (e.g. "sin(x)" or a full
    /// statement block). Returns false when compilation fails or the
    /// backend has no eval support.
    virtual bool compile(const std::string& body) = 0;

    /// Evaluate into `out` (vec2 buffer from makeOutput or compatible):
    /// out[i] = (x_i, f(x_i)), x_i spread evenly over [xMin, xMax].
    virtual void eval(GpuBuf out, double xMin, double xMax,
                      uint32_t count) = 0;

    /// Allocate an output buffer holding `count` vec2 points.
    /// The token stays valid for the services' lifetime.
    [[nodiscard]] virtual GpuBuf makeOutput(uint32_t count) const = 0;

    [[nodiscard]] virtual bool ready() const noexcept = 0;
    [[nodiscard]] virtual bool compiled() const noexcept = 0;
};

} // namespace volcano::render::primitives
