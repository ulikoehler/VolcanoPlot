// volcano/plot/Normalize.hpp — matplotlib-style color normalizations
//
// A Normalize maps data values to [0, 1] for colormap sampling.
// Equivalent to matplotlib.colors.Normalize and its subclasses.
//
// Usage:
//   auto norm = std::make_shared<LogNorm>(1.0f, 1000.0f);
//   float t = (*norm)(42.0f);  // → ~0.41
//   Color c = cmap.sample(t);
//
// Plots with colormapped data accept an optional std::shared_ptr<Normalize>
// in their config. If set, the norm replaces the default linear (vmin,vmax)
// mapping. If the norm's vmin/vmax are unset (NaN), the plot autoscales
// them from the data.
#pragma once

#include "volcano/plot/Types.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace volcano::plot {

/// Abstract base class for data normalization.
/// Maps a data value to [0, 1] (clipped if clip=true).
class Normalize {
public:
    virtual ~Normalize() = default;

    /// Forward map: data value → [0, 1] (clipped if clip_ is true).
    [[nodiscard]] virtual float operator()(float v) const = 0;

    /// Inverse map: [0, 1] → data value. Used by colorbar tick generation.
    [[nodiscard]] virtual float inverse(float t) const = 0;

    /// Set vmin/vmax from data (only if they are currently NaN/unset).
    virtual void autoscale(const std::vector<float>& data);

    /// Set vmin/vmax from data unconditionally (override any existing values).
    virtual void autoscaleForce(const std::vector<float>& data);

    /// Set vmin and vmax explicitly.
    void setVmin(float v) { vmin_ = v; }
    void setVmax(float v) { vmax_ = v; }

    [[nodiscard]] float vmin() const { return vmin_; }
    [[nodiscard]] float vmax() const { return vmax_; }
    [[nodiscard]] bool clip() const { return clip_; }
    void setClip(bool c) { clip_ = c; }

    /// Whether vmin/vmax have been set (non-NaN).
    [[nodiscard]] bool hasRange() const;

protected:
    float vmin_ = std::nanf("");
    float vmax_ = std::nanf("");
    bool clip_ = true;

    /// Clamp t to [0, 1] if clip_ is true.
    [[nodiscard]] float maybeClip(float t) const {
        return clip_ ? std::clamp(t, 0.0f, 1.0f) : t;
    }

    /// Compute min/max of data (ignoring NaN), setting vmin_/vmax_.
    void computeRange(const std::vector<float>& data);
};

// ─── Linear normalization (default) ──────────────────────────────────────

/// Linear normalization: t = (v - vmin) / (vmax - vmin).
/// This is the default behavior when no norm is specified.
class NormalizeLinear : public Normalize {
public:
    NormalizeLinear() = default;
    NormalizeLinear(float vmin, float vmax) { vmin_ = vmin; vmax_ = vmax; }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
};

/// NoNorm: pass-through. Values are assumed to be in [0, 1] already
/// (or direct colormap indices for ListedColormap). Used when data
/// should not be rescaled.
class NoNorm : public Normalize {
public:
    NoNorm() = default;
    NoNorm(float vmin, float vmax) { vmin_ = vmin; vmax_ = vmax; clip_ = false; }

    [[nodiscard]] float operator()(float v) const override { return v; }
    [[nodiscard]] float inverse(float t) const override { return t; }
};

// ─── Logarithmic normalization ───────────────────────────────────────────

/// LogNorm: t = (log10(v) - log10(vmin)) / (log10(vmax) - log10(vmin)).
/// Values <= 0 are clipped to 0 (or mapped to the under color if clip=false).
class LogNorm : public Normalize {
public:
    LogNorm() = default;
    LogNorm(float vmin, float vmax) { vmin_ = vmin; vmax_ = vmax; }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
};

// ─── Power normalization ─────────────────────────────────────────────────

/// PowerNorm: gamma power-law mapping.
/// For vmin >= 0: t = (log(v/vmin) / log(vmax/vmin))^gamma
/// For vmin < 0 < vmax: uses the sign-aware variant.
class PowerNorm : public Normalize {
public:
    explicit PowerNorm(float gamma = 1.0f) : gamma_(gamma) {}
    PowerNorm(float gamma, float vmin, float vmax) : gamma_(gamma) {
        vmin_ = vmin; vmax_ = vmax;
    }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] float gamma() const { return gamma_; }

private:
    float gamma_;
};

// ─── Symmetric log normalization ─────────────────────────────────────────

/// SymLogNorm: symmetric log scale with a linear region near zero.
/// |v| <= linthresh → linear; |v| > linthresh → log10.
/// linscale scales the linear region width.
class SymLogNorm : public Normalize {
public:
    explicit SymLogNorm(float linthresh = 1.0f, float linscale = 1.0f)
        : linthresh_(linthresh), linscale_(linscale) {}
    SymLogNorm(float linthresh, float linscale, float vmin, float vmax)
        : linthresh_(linthresh), linscale_(linscale) {
        vmin_ = vmin; vmax_ = vmax;
    }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] float linthresh() const { return linthresh_; }
    [[nodiscard]] float linscale() const { return linscale_; }

private:
    float linthresh_;
    float linscale_;

    /// Transform a single value to the symmetric-log space.
    [[nodiscard]] float transform(float v) const;
};

// ─── Asinh normalization ─────────────────────────────────────────────────

/// AsinhNorm: inverse hyperbolic sine scaling.
/// t = (asinh(v/w) - asinh(vmin/w)) / (asinh(vmax/w) - asinh(vmin/w))
/// where w = linear_width. Handles data spanning zero gracefully.
class AsinhNorm : public Normalize {
public:
    explicit AsinhNorm(float linearWidth = 1.0f) : linearWidth_(linearWidth) {}
    AsinhNorm(float linearWidth, float vmin, float vmax)
        : linearWidth_(linearWidth) { vmin_ = vmin; vmax_ = vmax; }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] float linearWidth() const { return linearWidth_; }

private:
    float linearWidth_;
};

// ─── Boundary normalization (discrete) ───────────────────────────────────

/// BoundaryNorm: maps values to discrete bin indices.
/// boundaries = [b0, b1, ..., bn] define n bins.
/// Returns (i + 0.5) / n for bin i, so each bin samples the center of
/// its colormap segment. Values below b0 → 0, above bn → 1 (if clip).
class BoundaryNorm : public Normalize {
public:
    explicit BoundaryNorm(std::vector<float> boundaries) : boundaries_(std::move(boundaries)) {}

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] const std::vector<float>& boundaries() const { return boundaries_; }
    [[nodiscard]] size_t numBins() const { return boundaries_.size() > 1 ? boundaries_.size() - 1 : 0; }

    // BoundaryNorm does not use vmin/vmax autoscale.
    void autoscale(const std::vector<float>&) override {}
    void autoscaleForce(const std::vector<float>&) override {}

private:
    std::vector<float> boundaries_;
};

// ─── Centered normalization ──────────────────────────────────────────────

/// CenteredNorm: linear normalization centered on a value with a fixed
/// half-range. t = (v - (center - vrange)) / (2 * vrange).
/// If vrange is NaN, it is auto-computed as max(|v - center|) from data.
class CenteredNorm : public Normalize {
public:
    explicit CenteredNorm(float center = 0.0f, float vrange = std::nanf(""))
        : center_(center), vrange_(vrange) {}

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] float center() const { return center_; }
    [[nodiscard]] float vrange() const { return vrange_; }

    void autoscale(const std::vector<float>& data) override;
    void autoscaleForce(const std::vector<float>& data) override;

private:
    float center_;
    float vrange_;
};

// ─── Two-slope normalization ──────────────────────────────────────────────

/// TwoSlopeNorm: piecewise linear with a pivot at vcenter.
/// v < vcenter:  t = (v - vmin) / (vcenter - vmin) * 0.5
/// v >= vcenter: t = 0.5 + (v - vcenter) / (vmax - vcenter) * 0.5
class TwoSlopeNorm : public Normalize {
public:
    explicit TwoSlopeNorm(float vcenter = 0.0f) : vcenter_(vcenter) {}
    TwoSlopeNorm(float vcenter, float vmin, float vmax) : vcenter_(vcenter) {
        vmin_ = vmin; vmax_ = vmax;
    }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;
    [[nodiscard]] float vcenter() const { return vcenter_; }

    void autoscale(const std::vector<float>& data) override;

private:
    float vcenter_;
};

// ─── Function normalization ──────────────────────────────────────────────

/// FuncNorm: user-supplied forward and inverse functions.
/// forward(v, vmin, vmax) → t; inverse(t, vmin, vmax) → v.
class FuncNorm : public Normalize {
public:
    using ForwardFn = std::function<float(float, float, float)>;
    using InverseFn = std::function<float(float, float, float)>;

    FuncNorm(ForwardFn fwd, InverseFn inv)
        : forward_(std::move(fwd)), inverse_(std::move(inv)) {}
    FuncNorm(ForwardFn fwd, InverseFn inv, float vmin, float vmax)
        : forward_(std::move(fwd)), inverse_(std::move(inv)) {
        vmin_ = vmin; vmax_ = vmax;
    }

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;

private:
    ForwardFn forward_;
    InverseFn inverse_;
};

// ─── Multi-normalization (chained) ───────────────────────────────────────

/// MultiNorm: chains multiple norms. The output of norm[i] feeds into
/// norm[i+1]. The first norm's vmin/vmax are autoscaled from data;
/// subsequent norms operate on the [0,1] output of the previous.
class MultiNorm : public Normalize {
public:
    explicit MultiNorm(std::vector<std::shared_ptr<Normalize>> norms)
        : norms_(std::move(norms)) {}

    [[nodiscard]] float operator()(float v) const override;
    [[nodiscard]] float inverse(float t) const override;

    void autoscale(const std::vector<float>& data) override;
    void autoscaleForce(const std::vector<float>& data) override;

    [[nodiscard]] const std::vector<std::shared_ptr<Normalize>>& norms() const {
        return norms_;
    }

private:
    std::vector<std::shared_ptr<Normalize>> norms_;
};

// ─── Convenience typedef ──────────────────────────────────────────────────

/// Default linear norm (the name "Normalize" matches matplotlib).
using NormalizeDefault = NormalizeLinear;

/// Factory helpers.
namespace norms {
    /// Create a linear norm with optional explicit range.
    inline std::shared_ptr<Normalize> linear(float vmin = std::nanf(""),
                                             float vmax = std::nanf("")) {
        auto n = std::make_shared<NormalizeLinear>();
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }

    /// Create a log norm with optional explicit range.
    inline std::shared_ptr<Normalize> log(float vmin = std::nanf(""),
                                          float vmax = std::nanf("")) {
        auto n = std::make_shared<LogNorm>();
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }

    /// Create a power norm.
    inline std::shared_ptr<Normalize> power(float gamma,
                                            float vmin = std::nanf(""),
                                            float vmax = std::nanf("")) {
        auto n = std::make_shared<PowerNorm>(gamma);
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }

    /// Create a symmetric log norm.
    inline std::shared_ptr<Normalize> symlog(float linthresh = 1.0f,
                                             float linscale = 1.0f,
                                             float vmin = std::nanf(""),
                                             float vmax = std::nanf("")) {
        auto n = std::make_shared<SymLogNorm>(linthresh, linscale);
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }

    /// Create an asinh norm.
    inline std::shared_ptr<Normalize> asinh(float linearWidth = 1.0f,
                                            float vmin = std::nanf(""),
                                            float vmax = std::nanf("")) {
        auto n = std::make_shared<AsinhNorm>(linearWidth);
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }

    /// Create a boundary norm.
    inline std::shared_ptr<Normalize> boundary(std::vector<float> b) {
        return std::make_shared<BoundaryNorm>(std::move(b));
    }

    /// Create a centered norm.
    inline std::shared_ptr<Normalize> centered(float center = 0.0f,
                                               float vrange = std::nanf("")) {
        return std::make_shared<CenteredNorm>(center, vrange);
    }

    /// Create a two-slope norm.
    inline std::shared_ptr<Normalize> twoslope(float vcenter = 0.0f,
                                               float vmin = std::nanf(""),
                                               float vmax = std::nanf("")) {
        auto n = std::make_shared<TwoSlopeNorm>(vcenter);
        if (!std::isnan(vmin)) n->setVmin(vmin);
        if (!std::isnan(vmax)) n->setVmax(vmax);
        return n;
    }
} // namespace norms

} // namespace volcano::plot
