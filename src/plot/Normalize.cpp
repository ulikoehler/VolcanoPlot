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

    float t;
    if (vmin_ > 0.0f) {
        // All positive: log-power mapping.
        if (v <= 0.0f) return maybeClip(0.0f);
        float lr = std::log(v / vmin_) / std::log(vmax_ / vmin_);
        t = std::pow(lr, gamma_);
    } else if (vmax_ < 0.0f) {
        // All negative: mirror.
        if (v >= 0.0f) return maybeClip(1.0f);
        float lr = std::log(-v / -vmin_) / std::log(-vmax_ / -vmin_);
        t = 1.0f - std::pow(1.0f - lr, gamma_);
    } else {
        // Straddles zero or vmin==0: linear ratio with gamma power.
        float ratio = (v - vmin_) / span;
        t = std::pow(std::clamp(ratio, 0.0f, 1.0f), 1.0f / gamma_);
    }
    return maybeClip(t);
}

float PowerNorm::inverse(float t) const {
    if (!hasRange()) return 0.0f;
    float span = vmax_ - vmin_;
    if (span == 0.0f) return 0.0f;

    if (vmin_ > 0.0f) {
        float lr = std::pow(t, 1.0f / gamma_);
        return vmin_ * std::pow(vmax_ / vmin_, lr);
    } else if (vmax_ < 0.0f) {
        float lr = 1.0f - std::pow(1.0f - t, 1.0f / gamma_);
        return -(-vmin_ * std::pow(-vmax_ / -vmin_, lr));
    } else {
        float ratio = std::pow(t, gamma_);
        return vmin_ + ratio * span;
    }
}

// ─── SymLogNorm ───────────────────────────────────────────────────────────

float SymLogNorm::transform(float v) const {
    // Symmetric log transform: linear near zero, log outside.
    // The linear region spans [-linthresh, +linthresh] and is scaled
    // by linscale to match the log curve at the boundary.
    float log_thresh = std::log10(linthresh_);
    // Avoid division by zero when linthresh=1 (log10(1)=0).
    // In that case, the linear region maps 1:1 to the transformed space.
    float scaled_lin = (log_thresh != 0.0f)
        ? linthresh_ * linscale_ / log_thresh
        : linthresh_ * linscale_;
    if (std::abs(v) <= linthresh_) {
        return v / scaled_lin * linthresh_;
    }
    float sign = v > 0.0f ? 1.0f : -1.0f;
    return sign * (std::log10(std::abs(v)) - log_thresh + scaled_lin);
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
    float log_thresh = std::log10(linthresh_);
    float scaled_lin = (log_thresh != 0.0f)
        ? linthresh_ * linscale_ / log_thresh
        : linthresh_ * linscale_;
    if (std::abs(tv) <= scaled_lin) {
        return tv / linthresh_ * scaled_lin;
    }
    float sign = tv > 0.0f ? 1.0f : -1.0f;
    return sign * std::pow(10.0f, std::abs(tv) - scaled_lin + log_thresh);
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
    size_t n = numBins();
    if (n == 0) return 0.0f;
    // Find the bin containing v.
    if (v < boundaries_.front()) return clip_ ? 0.0f : -0.5f / n;
    if (v >= boundaries_.back()) return clip_ ? 1.0f : 1.0f;
    for (size_t i = 0; i < n; ++i) {
        if (v < boundaries_[i + 1]) {
            return (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        }
    }
    return 1.0f;
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
