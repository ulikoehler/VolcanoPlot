// volcano/plot/Scale.hpp — matplotlib-style axis scales
//
// A scale maps data values to "display space" via a monotone forward
// function; the renderer then maps display space linearly to pixels.
// Closed-form scales (log, symlog, logit, asinh, mercator) are evaluated
// in the vertex shader via a scale code; `function`/`functionlog` scales
// are evaluated on the CPU for ticks/annotations and applied to plot data
// by plots that support them.
#pragma once

#include "volcano/plot/Types.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace volcano::plot {

/// Scale kinds. The integer values are the shader scale codes.
enum class ScaleKind : int {
    Linear = 0,
    Log = 1,        ///< log10(x); params unused
    Symlog = 2,     ///< param1 = linthresh, param2 = linscale
    Logit = 3,      ///< log(p/(1-p)), clamped to (eps, 1-eps)
    Asinh = 4,      ///< param1 = linear width a: a*asinh(x/a)
    Mercator = 5,   ///< latitude: ln(tan(pi/4 + phi/2)), |phi| <= ~85.05 deg
    Function = 6,   ///< user-provided forward/inverse (CPU-side)
    FunctionLog = 7 ///< user forward fn applied to log10(x) (CPU-side)
};

/// An axis scale: a monotone forward/inverse pair plus shader parameters.
struct AxisScale {
    ScaleKind kind = ScaleKind::Linear;
    float param1 = 1.0f, param2 = 1.0f, param3 = 10.0f;

    /// User functions for Function/FunctionLog kinds.
    std::function<float(float)> forwardFn, inverseFn;

    /// data -> display space.
    [[nodiscard]] float forward(float v) const;
    /// display space -> data.
    [[nodiscard]] float inverse(float v) const;

    /// Whether `v` lies inside the scale's data domain. Log scales drop
    /// non-positive values, logit drops values outside (0, 1) —
    /// matplotlib masks such points at data-conversion time.
    [[nodiscard]] bool inDomain(float v) const noexcept {
        switch (kind) {
        case ScaleKind::Log:
        case ScaleKind::FunctionLog:
            // Nonpositive points are always dropped (the transform
            // yields NaN). mpl's nonpositive='clip'|'mask' governs
            // autoscale clamping only, not point visibility.
            return v > 0.0f;
        case ScaleKind::Logit:
            return v > 0.0f && v < 1.0f;
        default:
            return true;
        }
    }
    /// Whether the scale can reject any data values (inDomain is not
    /// always true).
    [[nodiscard]] bool clipsDomain() const noexcept {
        return kind == ScaleKind::Log || kind == ScaleKind::FunctionLog ||
               kind == ScaleKind::Logit;
    }

    [[nodiscard]] bool isLinear() const { return kind == ScaleKind::Linear; }
    /// Whether the GPU shaders implement this scale.
    [[nodiscard]] bool shaderSupported() const {
        return kind != ScaleKind::Function && kind != ScaleKind::FunctionLog;
    }

    // --- Factories (matplotlib set_xscale names) ---
    static AxisScale linear() { return {}; }
    /// Log scale; `base` is the decade base (mpl LogScale `base`,
    /// default 10) stored in param1. `mask` is mpl
    /// `nonpositive='mask'` (drop nonpositive points) vs the default
    /// 'clip' — stored in param2. The forward/inverse pair remains
    /// base-10 since a constant log-space rescale is display-identical.
    static AxisScale log(float base = 10.0f, bool mask = false) {
        return {ScaleKind::Log, base, mask ? 1.0f : 0.0f, 10, {}, {}};
    }
    /// symlog: linear within ±linthresh, log beyond. linscale stretches
    /// the linear region; `base` is the decade base (param3, mpl
    /// SymmetricalLogScale base, default 10).
    static AxisScale symlog(float linthresh = 2.0f, float linscale = 1.0f,
                            float base = 10.0f) {
        return {ScaleKind::Symlog, linthresh, linscale, base, {}, {}};
    }
    static AxisScale logit() {
        return {ScaleKind::Logit, 1, 1, 10, {}, {}};
    }
    /// asinh: linear_width controls the linear region near zero.
    static AxisScale asinh(float linearWidth = 1.0f) {
        return {ScaleKind::Asinh, linearWidth, 1, 10, {}, {}};
    }
    static AxisScale mercator() {
        return {ScaleKind::Mercator, 1, 1, 10, {}, {}};
    }
    static AxisScale function(std::function<float(float)> fwd,
                              std::function<float(float)> inv) {
        return {ScaleKind::Function, 1, 1, 10, std::move(fwd),
                std::move(inv)};
    }
    /// functionlog: mpl FuncScaleLog — display is log_base(forward(x));
    /// `base` stored in param1 (default 10).
    static AxisScale functionlog(std::function<float(float)> fwd,
                                 std::function<float(float)> inv,
                                 float base = 10.0f) {
        return {ScaleKind::FunctionLog, base, 1, 10, std::move(fwd),
                std::move(inv)};
    }
};

/// Scale-aware tick locator: returns nice tick positions in data space.
[[nodiscard]] std::vector<float>
scaleTicks(const AxisScale& scale, float vmin, float vmax, int nbins = 9);

/// Nice step size for the scale at the given range (used for label format).
[[nodiscard]] float
scaleTickStep(const AxisScale& scale, float vmin, float vmax, int nbins = 9);

/// Whether a data point is valid under both axis scales.
[[nodiscard]] inline bool pointInDomain(Point2D p, const AxisScale& sx,
                                        const AxisScale& sy) noexcept {
    return sx.inDomain(p.x) && sy.inDomain(p.y);
}

/// Replace out-of-domain points with NaN sentinels so downstream
/// consumers split polylines / skip markers (matplotlib masks
/// non-positive data on log axes at conversion time).
[[nodiscard]] std::vector<Point2D>
maskPointsForScales(std::span<const Point2D> points, const AxisScale& sx,
                    const AxisScale& sy);

} // namespace volcano::plot
