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

float symlogFwd(float v, float linthresh, float linscale) {
    float a = std::fabs(v);
    if (a <= linthresh) return linscale * v;
    return std::copysign(linthresh * (linscale + std::log10(a / linthresh)), v);
}
float symlogInv(float v, float linthresh, float linscale) {
    float a = std::fabs(v);
    float linEdge = linscale * linthresh;
    if (a <= linEdge) return v / linscale;
    // inverse of linthresh*(linscale + log10(a'/linthresh)) = a
    return std::copysign(linthresh * std::pow(10.0f, a / linthresh - linscale), v);
}
} // namespace

float AxisScale::forward(float v) const {
    switch (kind) {
    case ScaleKind::Linear: return v;
    case ScaleKind::Log:    return std::log10(std::max(v, 1e-30f));
    case ScaleKind::Symlog: return symlogFwd(v, param1, param2);
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
        return forwardFn ? forwardFn(std::log10(std::max(v, 1e-30f))) : v;
    }
    return v;
}

float AxisScale::inverse(float v) const {
    switch (kind) {
    case ScaleKind::Linear: return v;
    case ScaleKind::Log:    return std::pow(10.0f, v);
    case ScaleKind::Symlog: return symlogInv(v, param1, param2);
    case ScaleKind::Logit:  return 1.0f / (1.0f + std::exp(-v));
    case ScaleKind::Asinh:  return param1 * std::sinh(v / std::max(param1, 1e-30f));
    case ScaleKind::Mercator:
        return 2.0f * std::atan(std::exp(v)) - kPi / 2.0f;
    case ScaleKind::Function:
        return inverseFn ? inverseFn(v) : v;
    case ScaleKind::FunctionLog:
        return inverseFn ? std::pow(10.0f, inverseFn(v)) : v;
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
        // Ticks at powers of 10 covering the range.
        std::vector<float> out;
        if (lo <= 0.0f) return linearTicks(lo, hi, nbins);
        int e0 = static_cast<int>(std::floor(std::log10(lo)));
        int e1 = static_cast<int>(std::ceil(std::log10(hi)));
        if (e1 - e0 <= nbins) {
            for (int e = e0; e <= e1; ++e) {
                float t = std::pow(10.0f, static_cast<float>(e));
                if (t >= lo - 1e-9f && t <= hi + 1e-9f) out.push_back(t);
            }
        } else {
            // Too many decades: thin out.
            int stride = (e1 - e0 + nbins - 1) / nbins;
            for (int e = e0; e <= e1; e += stride)
                out.push_back(std::pow(10.0f, static_cast<float>(e)));
        }
        return out;
    }
    case ScaleKind::Symlog: {
        // Linear ticks inside ±linthresh, decade ticks beyond.
        float lt = scale.param1;
        std::vector<float> out = linearTicks(
            std::max(lo, -lt), std::min(hi, lt), nbins / 2 + 1);
        if (hi > lt) {
            int e0 = static_cast<int>(std::ceil(std::log10(lt)));
            int e1 = static_cast<int>(std::floor(std::log10(hi)));
            for (int e = e0; e <= e1; ++e)
                out.push_back(std::pow(10.0f, static_cast<float>(e)));
        }
        if (lo < -lt) {
            int e0 = static_cast<int>(std::ceil(std::log10(lt)));
            int e1 = static_cast<int>(std::floor(std::log10(-lo)));
            for (int e = e0; e <= e1; ++e)
                out.push_back(-std::pow(10.0f, static_cast<float>(e)));
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
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
    case ScaleKind::Function:
    case ScaleKind::FunctionLog:
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
