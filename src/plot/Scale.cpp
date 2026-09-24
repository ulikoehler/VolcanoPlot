// volcano/plot/Scale.cpp
#include "volcano/plot/Scale.hpp"
#include "volcano/plot/Ticks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {
constexpr float kEps = 1e-7f;
constexpr float kPi = 3.14159265358979323846f;
// Mercator latitude limit (~85.05113 deg in radians).
constexpr float kMercatorLimit = 1.48442223f;

/// mpl SymmetricalLogTransform: linscale_adj = linscale/(1 - base⁻¹)
/// normalizes the linear-region slope so the transform is C¹-smooth at
/// ±linthresh.
float symlogFwd(float v, float linthresh, float linscale, float base) {
    float adj = linscale / (1.0f - std::pow(base, -1.0f));
    float a = std::fabs(v);
    if (a <= linthresh) return adj * v;
    return std::copysign(
        linthresh * (adj + std::log(a / linthresh) / std::log(base)), v);
}
float symlogInv(float v, float linthresh, float linscale, float base) {
    float adj = linscale / (1.0f - std::pow(base, -1.0f));
    float a = std::fabs(v);
    // invlinthresh = transform(linthresh) = linthresh * adj.
    if (a <= linthresh * adj) return v / adj;
    return std::copysign(
        linthresh * std::pow(base, a / linthresh - adj), v);
}
} // namespace

float AxisScale::forward(float v) const {
    switch (kind) {
    case ScaleKind::Linear: return v;
    case ScaleKind::Log:    return std::log10(std::max(v, 1e-30f));
    case ScaleKind::Symlog: return symlogFwd(v, param1, param2, param3);
    case ScaleKind::Logit: {
        float p = std::clamp(v, kEps, 1.0f - kEps);
        return std::log(p / (1.0f - p));
    }
    case ScaleKind::Asinh:
        return param1 * std::asinh(v / std::max(param1, 1e-30f));
    case ScaleKind::Mercator: {
        float phi = std::clamp(v, -kMercatorLimit, kMercatorLimit);
        return std::log(std::tan(kPi / 4.0f + phi / 2.0f));
    }
    case ScaleKind::Function:
        return forwardFn ? forwardFn(v) : v;
    case ScaleKind::FunctionLog:
        // mpl FuncScaleLog: transform = FuncTransform + LogTransform(base)
        // → log_base(forward(v)).
        if (!forwardFn) return v;
        {
            float base = param1 > 0.0f ? param1 : 10.0f;
            return std::log(std::max(forwardFn(v), 1e-30f)) /
                   std::log(base);
        }
    }
    return v;
}

float AxisScale::inverse(float v) const {
    switch (kind) {
    case ScaleKind::Linear: return v;
    case ScaleKind::Log:    return std::pow(10.0f, v);
    case ScaleKind::Symlog: return symlogInv(v, param1, param2, param3);
    case ScaleKind::Logit:  return 1.0f / (1.0f + std::exp(-v));
    case ScaleKind::Asinh:  return param1 * std::sinh(v / std::max(param1, 1e-30f));
    case ScaleKind::Mercator:
        return 2.0f * std::atan(std::exp(v)) - kPi / 2.0f;
    case ScaleKind::Function:
        return inverseFn ? inverseFn(v) : v;
    case ScaleKind::FunctionLog:
        // inverse of log_base(f(x)): f⁻¹(base^v).
        if (!inverseFn) return v;
        {
            float base = param1 > 0.0f ? param1 : 10.0f;
            return inverseFn(std::pow(base, v));
        }
    }
    return v;
}

namespace {

/// Linear tick locator — matplotlib MaxNLocator semantics (nice steps
/// [1,2,2.5,5,10]×10^k, at most ~nbins+1 ticks).
std::vector<float> linearTicks(float vmin, float vmax, int nbins) {
    // mpl's default linear locator is AutoLocator: MaxNLocator with
    // steps=[1, 2, 2.5, 5, 10].
    MaxNLocator loc{nbins};
    loc.setSteps({1.0f, 2.0f, 2.5f, 5.0f, 10.0f});
    return loc.tickValues(vmin, vmax);
}

} // namespace

std::vector<float> scaleTicks(const AxisScale& scale, float vmin, float vmax,
                              int nbins) {
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    switch (scale.kind) {
    case ScaleKind::Log: {
        // Ticks at powers of `base` covering the range (mpl LogLocator).
        std::vector<float> out;
        if (lo <= 0.0f) return linearTicks(lo, hi, nbins);
        float base = scale.param1 > 0.0f ? scale.param1 : 10.0f;
        float lb = std::log(base);
        int e0 = static_cast<int>(std::floor(std::log(lo) / lb));
        int e1 = static_cast<int>(std::ceil(std::log(hi) / lb));
        if (e1 - e0 <= nbins) {
            for (int e = e0; e <= e1; ++e) {
                float t = std::pow(base, static_cast<float>(e));
                if (t >= lo - 1e-9f && t <= hi + 1e-9f) out.push_back(t);
            }
        } else {
            // Too many decades: thin out.
            int stride = (e1 - e0 + nbins - 1) / nbins;
            for (int e = e0; e <= e1; e += stride)
                out.push_back(std::pow(base, static_cast<float>(e)));
        }
        return out;
    }
    case ScaleKind::Symlog:
        // mpl SymmetricalLogLocator: decade ticks beyond ±linthresh,
        // a lone 0 inside the linear segment.
        return SymmetricalLogLocator{scale.param1,
                                     scale.param3 > 1.0f ? scale.param3
                                                         : 10.0f}
            .tickValues(lo, hi);
    case ScaleKind::Logit: {
        // Nice probability ticks.
        static const float kProbs[] = {0.5f, 0.9f, 0.99f, 0.999f, 0.9999f,
                                       0.1f, 0.01f, 0.001f, 0.0001f};
        std::vector<float> out;
        for (float p : kProbs)
            if (p >= lo && p <= hi) out.push_back(p);
        if (out.empty()) out = linearTicks(lo, hi, nbins);
        std::sort(out.begin(), out.end());
        return out;
    }
    case ScaleKind::Asinh: {
        // Linear ticks near zero, decade ticks in the log tails.
        float a = scale.param1;
        std::vector<float> out = linearTicks(
            std::max(lo, -a), std::min(hi, a), nbins / 2 + 1);
        for (int sgn : {1, -1}) {
            float edge = sgn > 0 ? hi : -lo;
            if (edge > a) {
                int e0 = static_cast<int>(std::ceil(std::log10(a)));
                int e1 = static_cast<int>(std::floor(std::log10(edge)));
                for (int e = e0; e <= e1; ++e)
                    out.push_back(sgn * std::pow(10.0f, static_cast<float>(e)));
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    case ScaleKind::Mercator:
        // Latitude ticks every 30 degrees within range.
        return linearTicks(lo, hi, nbins);
    case ScaleKind::FunctionLog: {
        // mpl FuncScaleLog inherits LogScale's locator: powers of base.
        std::vector<float> out;
        float base = scale.param1 > 0.0f ? scale.param1 : 10.0f;
        float lb = std::log(base);
        if (lo <= 0.0f) return linearTicks(lo, hi, nbins);
        int e0 = static_cast<int>(std::floor(std::log(lo) / lb));
        int e1 = static_cast<int>(std::ceil(std::log(hi) / lb));
        int stride = std::max(1, (e1 - e0 + nbins - 1) / nbins);
        for (int e = e0; e <= e1; e += stride)
            out.push_back(std::pow(base, static_cast<float>(e)));
        return out;
    }
    case ScaleKind::Function:
        // Nice ticks in transformed space, mapped back via inverse.
        if (scale.forwardFn && scale.inverseFn) {
            float t0 = scale.forward(lo), t1 = scale.forward(hi);
            std::vector<float> out;
            for (float t : linearTicks(std::min(t0, t1), std::max(t0, t1), nbins))
                out.push_back(scale.inverse(t));
            std::sort(out.begin(), out.end());
            return out;
        }
        [[fallthrough]];
    case ScaleKind::Linear:
    default:
        return linearTicks(lo, hi, nbins);
    }
}

float scaleTickStep(const AxisScale& scale, float vmin, float vmax, int nbins) {
    auto ticks = scaleTicks(scale, vmin, vmax, nbins);
    if (ticks.size() >= 2)
        return ticks[1] - ticks[0];
    float span = vmax - vmin;
    return span > 0 ? span / std::max(nbins - 1, 1) : 1.0f;
}

std::vector<Point2D> maskPointsForScales(std::span<const Point2D> points,
                                         const AxisScale& sx,
                                         const AxisScale& sy) {
    std::vector<Point2D> out;
    out.reserve(points.size());
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    for (Point2D p : points) {
        if (!sx.inDomain(p.x) || !sy.inDomain(p.y) ||
            !std::isfinite(p.x) || !std::isfinite(p.y))
            out.push_back({kNaN, kNaN});
        else
            out.push_back(p);
    }
    return out;
}

} // namespace volcano::plot
