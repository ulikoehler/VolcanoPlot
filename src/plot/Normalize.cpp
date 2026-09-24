// volcano/plot/Normalize.cpp — normalization implementations
#include "volcano/plot/Normalize.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

// ─── Base class ───────────────────────────────────────────────────────────

bool Normalize::hasRange() const {
    return !std::isnan(vmin_) && !std::isnan(vmax_);
}

void Normalize::computeRange(const std::vector<float>& data) {
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (float v : data) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo > hi) { lo = 0.0f; hi = 1.0f; }
    vmin_ = lo;
    vmax_ = hi;
}

void Normalize::autoscale(const std::vector<float>& data) {
    if (!std::isnan(vmin_) && !std::isnan(vmax_)) return;
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (float v : data) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo > hi) { lo = 0.0f; hi = 1.0f; }
    if (std::isnan(vmin_)) vmin_ = lo;
    if (std::isnan(vmax_)) vmax_ = hi;
}

void Normalize::autoscaleForce(const std::vector<float>& data) {
    vmin_ = std::nanf("");
    vmax_ = std::nanf("");
    autoscale(data);
}

// ─── NormalizeLinear ──────────────────────────────────────────────────────

float NormalizeLinear::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    float span = vmax_ - vmin_;
    if (span == 0.0f) return 0.0f;
    return maybeClip((v - vmin_) / span);
}

float NormalizeLinear::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    return vmin_ + t * (vmax_ - vmin_);
}

// ─── LogNorm ──────────────────────────────────────────────────────────────

float LogNorm::operator()(float v) const {
    if (!hasRange() || v <= 0.0f || vmin_ <= 0.0f || vmax_ <= 0.0f) {
        return maybeClip(v <= 0.0f ? 0.0f : 1.0f);
    }
    float lmin = std::log10(vmin_);
    float lmax = std::log10(vmax_);
    float span = lmax - lmin;
    if (span == 0.0f) return 0.0f;
    return maybeClip((std::log10(v) - lmin) / span);
}

float LogNorm::inverse(float t) const {
    if (!hasRange() || vmin_ <= 0.0f || vmax_ <= 0.0f) return 0.0f;
    float lmin = std::log10(vmin_);
    float lmax = std::log10(vmax_);
    return std::pow(10.0f, lmin + t * (lmax - lmin));
}

// ─── PowerNorm ────────────────────────────────────────────────────────────

float PowerNorm::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    float span = vmax_ - vmin_;
    if (span == 0.0f) return 0.0f;

    // mpl PowerNorm: t = (v - vmin) / span, then t^gamma where t > 0
    // (below vmin the mapping stays linear). clip clamps input to
    // [vmin, vmax] first.
    if (clip_) v = std::clamp(v, vmin_, vmax_);
    float t = (v - vmin_) / span;
    if (t > 0.0f) t = std::pow(t, gamma_);
    return t;
}

float PowerNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    float span = vmax_ - vmin_;
    if (span == 0.0f) return 0.0f;

    if (t > 0.0f) t = std::pow(t, 1.0f / gamma_);
    return vmin_ + t * span;
}

// ─── SymLogNorm ───────────────────────────────────────────────────────────

float SymLogNorm::transform(float v) const {
    // mpl SymmetricalLogTransform (base=10):
    //   _linscale_adj = linscale / (1 - base^-1)
    //   |v| <= linthresh : T(v) = v * _linscale_adj
    //   |v| >  linthresh : T(v) = sign(v) * linthresh *
    //                       (_linscale_adj + log(|v|/linthresh)/log(base))
    const float base = 10.0f;
    const float adj = linscale_ / (1.0f - std::pow(base, -1.0f));
    if (std::abs(v) <= linthresh_) {
        return v * adj;
    }
    float sign = v > 0.0f ? 1.0f : -1.0f;
    return sign * linthresh_ *
           (adj + std::log(std::abs(v) / linthresh_) / std::log(base));
}

float SymLogNorm::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    float tmin = transform(vmin_);
    float tmax = transform(vmax_);
    float span = tmax - tmin;
    if (span == 0.0f) return 0.0f;
    return maybeClip((transform(v) - tmin) / span);
}

float SymLogNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    float tmin = transform(vmin_);
    float tmax = transform(vmax_);
    float tv = tmin + t * (tmax - tmin);
    // mpl InvertedSymmetricalLogTransform (base=10):
    //   invlinthresh = T(linthresh) = linthresh * _linscale_adj
    //   |tv| <= invlinthresh : v = tv / _linscale_adj
    //   |tv| >  invlinthresh : v = sign(tv) * linthresh *
    //                       base^(|tv|/linthresh - _linscale_adj)
    const float base = 10.0f;
    const float adj = linscale_ / (1.0f - std::pow(base, -1.0f));
    const float invlinthresh = linthresh_ * adj;
    if (std::abs(tv) <= invlinthresh) {
        return tv / adj;
    }
    float sign = tv > 0.0f ? 1.0f : -1.0f;
    return sign * linthresh_ *
           std::pow(base, std::abs(tv) / linthresh_ - adj);
}

// ─── AsinhNorm ────────────────────────────────────────────────────────────

float AsinhNorm::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    float w = linearWidth_;
    if (w == 0.0f) w = 1.0f;
    float amin = std::asinh(vmin_ / w);
    float amax = std::asinh(vmax_ / w);
    float span = amax - amin;
    if (span == 0.0f) return 0.0f;
    return maybeClip((std::asinh(v / w) - amin) / span);
}

float AsinhNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    float w = linearWidth_;
    if (w == 0.0f) w = 1.0f;
    float amin = std::asinh(vmin_ / w);
    float amax = std::asinh(vmax_ / w);
    return w * std::sinh(amin + t * (amax - amin));
}

// ─── BoundaryNorm ─────────────────────────────────────────────────────────

float BoundaryNorm::operator()(float v) const {
    if (numBins() == 0 || ncolors_ < 2) return 0.0f;
    const float vminB = boundaries_.front();
    const float vmaxB = boundaries_.back();
    // mpl: clip clamps the input into [vmin, vmax] before digitizing.
    const float vc = clip_ ? std::clamp(v, vminB, vmaxB) : v;
    // np.digitize(vc, boundaries) == searchsorted right.
    const long idx = static_cast<long>(
        std::upper_bound(boundaries_.begin(), boundaries_.end(), vc) -
        boundaries_.begin());
    long iret = idx - 1 + offset_;
    // Stretch region indices across the full LUT when ncolors exceeds the
    // number of regions (first region → 0, last → ncolors-1).
    if (ncolors_ > nRegions_ && nRegions_ > 0) {
        if (nRegions_ == 1) {
            if (iret == 0) iret = static_cast<long>((ncolors_ - 1) / 2);
        } else {
            iret = static_cast<long>(static_cast<double>(ncolors_ - 1) /
                                     static_cast<double>(nRegions_ - 1) *
                                     iret);
        }
    }
    // mpl applies the under/over guards to the (possibly clipped) value:
    // with clip, vc is already inside [vminB, vmaxB] so neither fires.
    if (vc < vminB) iret = -1;
    else if (vc > vmaxB) iret = clip_ ? static_cast<long>(ncolors_) - 1
                                      : static_cast<long>(ncolors_);
    return static_cast<float>(iret) / static_cast<float>(ncolors_ - 1);
}

float BoundaryNorm::inverse(float t) const {
    size_t n = numBins();
    if (n == 0) return 0.0f;
    size_t idx = static_cast<size_t>(std::clamp(t, 0.0f, 1.0f) * n);
    if (idx >= n) idx = n - 1;
    // Return the bin center.
    return 0.5f * (boundaries_[idx] + boundaries_[idx + 1]);
}

// ─── CenteredNorm ─────────────────────────────────────────────────────────

float CenteredNorm::operator()(float v) const {
    if (std::isnan(vrange_)) return 0.0f;
    float lo = center_ - vrange_;
    float span = 2.0f * vrange_;
    if (span == 0.0f) return 0.0f;
    return maybeClip((v - lo) / span);
}

float CenteredNorm::inverse(float t) const {
    if (std::isnan(vrange_)) return 0.0f;
    return (center_ - vrange_) + t * 2.0f * vrange_;
}

void CenteredNorm::autoscale(const std::vector<float>& data) {
    if (!std::isnan(vrange_)) return;
    float maxDist = 0.0f;
    for (float v : data) {
        if (std::isnan(v)) continue;
        maxDist = std::max(maxDist, std::abs(v - center_));
    }
    if (maxDist == 0.0f) maxDist = 1.0f;
    vrange_ = maxDist;
    vmin_ = center_ - vrange_;
    vmax_ = center_ + vrange_;
}

void CenteredNorm::autoscaleForce(const std::vector<float>& data) {
    vrange_ = std::nanf("");
    autoscale(data);
}

// ─── TwoSlopeNorm ─────────────────────────────────────────────────────────

float TwoSlopeNorm::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    if (v < vcenter_) {
        float span = vcenter_ - vmin_;
        if (span == 0.0f) return 0.0f;
        return maybeClip((v - vmin_) / span * 0.5f);
    } else {
        float span = vmax_ - vcenter_;
        if (span == 0.0f) return 0.5f;
        return maybeClip(0.5f + (v - vcenter_) / span * 0.5f);
    }
}

float TwoSlopeNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    if (t < 0.5f) {
        return vmin_ + (t / 0.5f) * (vcenter_ - vmin_);
    } else {
        return vcenter_ + ((t - 0.5f) / 0.5f) * (vmax_ - vcenter_);
    }
}

void TwoSlopeNorm::autoscale(const std::vector<float>& data) {
    if (!std::isnan(vmin_) && !std::isnan(vmax_)) return;
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (float v : data) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo > hi) { lo = 0.0f; hi = 1.0f; }
    if (std::isnan(vmin_)) vmin_ = lo;
    if (std::isnan(vmax_)) vmax_ = hi;
}

// ─── FuncNorm ─────────────────────────────────────────────────────────────

float FuncNorm::operator()(float v) const {
    if (!hasRange()) return 0.0f;
    return maybeClip(forward_(v, vmin_, vmax_));
}

float FuncNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    return inverse_(t, vmin_, vmax_);
}

// ─── MultiNorm ────────────────────────────────────────────────────────────

float MultiNorm::operator()(float v) const {
    if (norms_.empty()) return 0.0f;
    float t = v;
    for (const auto& n : norms_) {
        t = (*n)(t);
    }
    return t;
}

float MultiNorm::inverse(float t) const {
    if (norms_.empty()) return 0.0f;
    // Apply inverses in reverse order.
    for (auto it = norms_.rbegin(); it != norms_.rend(); ++it) {
        t = (*it)->inverse(t);
    }
    return t;
}

void MultiNorm::autoscale(const std::vector<float>& data) {
    if (norms_.empty()) return;
    // Autoscale the first norm from the raw data.
    norms_[0]->autoscale(data);
    // Subsequent norms operate on [0,1] output; autoscale them from
    // the transformed data.
    std::vector<float> transformed;
    transformed.reserve(data.size());
    for (float v : data) {
        if (std::isnan(v)) continue;
        transformed.push_back((*norms_[0])(v));
    }
    for (size_t i = 1; i < norms_.size(); ++i) {
        norms_[i]->autoscale(transformed);
        // Re-transform for the next norm.
        for (float& t : transformed) {
            t = (*norms_[i])(t);
        }
    }
    // Propagate range to base class for colorbar use.
    vmin_ = norms_[0]->vmin();
    vmax_ = norms_[0]->vmax();
}

void MultiNorm::autoscaleForce(const std::vector<float>& data) {
    if (norms_.empty()) return;
    norms_[0]->autoscaleForce(data);
    std::vector<float> transformed;
    transformed.reserve(data.size());
    for (float v : data) {
        if (std::isnan(v)) continue;
        transformed.push_back((*norms_[0])(v));
    }
    for (size_t i = 1; i < norms_.size(); ++i) {
        norms_[i]->autoscaleForce(transformed);
        for (float& t : transformed) {
            t = (*norms_[i])(t);
        }
    }
    vmin_ = norms_[0]->vmin();
    vmax_ = norms_[0]->vmax();
}

} // namespace volcano::plot
