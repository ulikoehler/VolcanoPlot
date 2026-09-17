// volcano/plot/Ticks.cpp — locator/formatter implementations
#include "volcano/plot/Ticks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <numeric>

namespace volcano::plot {

namespace {

/// Nice-number step (1, 2, 2.5, 5, 10 × 10^k) giving ≤ nbins+1 ticks.
float niceStep(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return 1.0f;
    float rawStep = (vmax - vmin) / std::max(nbins, 1);
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    const float niceSteps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
    float step = 10.0f * mag;
    for (float s : niceSteps) {
        float cand = s * mag;
        int n = int(std::floor(vmax / cand) - std::ceil(vmin / cand)) + 1;
        if (n <= nbins + 1) { step = cand; break; }
    }
    return step;
}

/// Ticks at multiples of step covering [vmin, vmax].
std::vector<float> steppedTicks(float vmin, float vmax, float step,
                                float offset = 0.0f) {
    std::vector<float> out;
    if (step <= 0.0f) return out;
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    float start = (std::ceil((lo - offset) / step) * step) + offset;
    for (float v = start; v <= hi + step * 1e-6f; v += step)
        out.push_back(std::round((v - offset) / step) * step + offset);
    return out;
}

/// Trim trailing zeros / decimal point: "1.500" → "1.5".
std::string trimZeros(std::string s) {
    if (s.find('.') == std::string::npos) return s;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

/// "%g"-style formatting via snprintf.
std::string gFormat(float v, int sigDigits = 6) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", sigDigits, double(v));
    return buf;
}

/// Integer mantissa when v is an exact power: returns exponent or nullopt.
std::optional<int> exactPower(float v, float base) {
    if (v <= 0.0f || base <= 0.0f || base == 1.0f) return std::nullopt;
    float e = std::log(v) / std::log(base);
    float r = std::round(e);
    if (std::abs(e - r) < 1e-4f &&
        std::abs(std::pow(base, r) - v) <= std::abs(v) * 1e-4f)
        return static_cast<int>(r);
    return std::nullopt;
}

} // namespace

// ─── Locators ───────────────────────────────────────────────────────────────

std::vector<float> FixedLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    for (float p : positions_)
        if (p >= lo && p <= hi) out.push_back(p);
    return out;
}

std::vector<float> LinearLocator::tickValues(float vmin, float vmax) const {
    int n = numTicks_ > 0 ? numTicks_ : 9;
    std::vector<float> out;
    if (n <= 1 || vmin >= vmax) {
        out.push_back(vmin);
        return out;
    }
    for (int i = 0; i < n; ++i)
        out.push_back(vmin + (vmax - vmin) * i / (n - 1));
    return out;
}

std::vector<float> MultipleLocator::tickValues(float vmin, float vmax) const {
    return steppedTicks(vmin, vmax, base_, offset_);
}

std::vector<float> IndexLocator::tickValues(float vmin, float vmax) const {
    return steppedTicks(vmin, vmax, base_, offset_);
}

std::vector<float> MaxNLocator::tickValues(float vmin, float vmax) const {
    return steppedTicks(vmin, vmax, niceStep(vmin, vmax, nbins_));
}

std::vector<float> LogLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    if (lo <= 0.0f) return MaxNLocator{}.tickValues(vmin, vmax);
    float lb = std::log(base_);
    int e0 = static_cast<int>(std::floor(std::log(lo) / lb));
    int e1 = static_cast<int>(std::ceil(std::log(hi) / lb));
    int stride = std::max(1, (e1 - e0 + numticks_ - 1) / numticks_);
    for (int e = e0; e <= e1; e += stride) {
        float t = std::pow(base_, static_cast<float>(e));
        if (t >= lo - 1e-9f && t <= hi * (1.0f + 1e-9f)) out.push_back(t);
    }
    return out;
}

std::vector<float> LogLocator::minorValues(float vmin, float vmax,
                                           std::span<const float> subs) const {
    std::vector<float> out;
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    if (lo <= 0.0f) return out;
    std::vector<float> s;
    if (subs.empty()) {
        for (int i = 2; i < int(base_ + 0.5f); ++i) s.push_back(float(i));
    } else {
        s.assign(subs.begin(), subs.end());
    }
    float lb = std::log(base_);
    int e0 = static_cast<int>(std::floor(std::log(lo) / lb));
    int e1 = static_cast<int>(std::ceil(std::log(hi) / lb));
    for (int e = e0; e <= e1; ++e) {
        float decade = std::pow(base_, static_cast<float>(e));
        for (float m : s) {
            float t = m * decade;
            if (t >= lo && t <= hi) out.push_back(t);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<float> SymmetricalLogLocator::tickValues(float vmin,
                                                   float vmax) const {
    // Linear ticks inside ±linthresh, decade ticks beyond.
    auto out = MaxNLocator{5}.tickValues(std::max(vmin, -linthresh_),
                                         std::min(vmax, linthresh_));
    for (int sgn : {1, -1}) {
        float edge = sgn > 0 ? vmax : -vmin;
        if (edge > linthresh_) {
            int e0 = static_cast<int>(std::ceil(std::log10(linthresh_)));
            int e1 = static_cast<int>(std::floor(std::log10(edge)));
            for (int e = e0; e <= e1; ++e)
                out.push_back(sgn * std::pow(10.0f, static_cast<float>(e)));
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<float> LogitLocator::tickValues(float vmin, float vmax) const {
    static const float kProbs[] = {0.5f, 0.9f, 0.99f, 0.999f, 0.9999f,
                                   0.1f, 0.01f, 0.001f, 0.0001f};
    std::vector<float> out;
    for (float p : kProbs)
        if (p >= vmin && p <= vmax) out.push_back(p);
    if (out.empty()) out = MaxNLocator{}.tickValues(vmin, vmax);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<float> AutoMinorLocator::between(std::span<const float> majors,
                                             float vmin, float vmax) const {
    std::vector<float> out;
    if (majors.size() < 2) return out;
    float lo = std::min(vmin, vmax), hi = std::max(vmin, vmax);
    for (size_t i = 0; i + 1 < majors.size(); ++i) {
        float step = majors[i + 1] - majors[i];
        // Auto ndivs: 5 subdivisions; 4 when step/5 isn't a round number.
        int nd = ndivs_ > 0 ? ndivs_ : 5;
        if (ndivs_ == 0) {
            float sub = step / 5.0f;
            // Prefer 4 subdivisions for steps like 0.4, 2.5 → .1, .5 ok for 5.
            if (std::abs(sub * 10.0f - std::round(sub * 10.0f)) > 1e-4f) nd = 4;
        }
        for (int k = 1; k < nd; ++k) {
            float t = majors[i] + step * k / nd;
            if (t >= lo && t <= hi) out.push_back(t);
        }
    }
    return out;
}

std::vector<float> AutoMinorLocator::tickValues(float vmin, float vmax) const {
    auto majors = MaxNLocator{}.tickValues(vmin, vmax);
    return between(majors, vmin, vmax);
}

// ─── Formatters ─────────────────────────────────────────────────────────────

std::string FixedFormatter::format(float, int pos) const {
    if (pos < 0 || static_cast<size_t>(pos) >= labels_.size()) return {};
    return labels_[static_cast<size_t>(pos)];
}

std::string FormatStrFormatter::format(float v, int) const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), fmt_.c_str(), double(v));
    return buf;
}

std::string StrMethodFormatter::format(float v, int pos) const {
    std::string out = tmpl_;
    auto replaceAll = [](std::string& s, std::string_view from,
                         const std::string& to) {
        size_t i = 0;
        while ((i = s.find(from, i)) != std::string::npos) {
            s.replace(i, from.size(), to);
            i += to.size();
        }
    };
    replaceAll(out, "{x}", gFormat(v));
    replaceAll(out, "{pos}", std::to_string(pos));
    return out;
}

// ─── ScalarFormatter ────────────────────────────────────────────────────────

void ScalarFormatter::setLocs(std::span<const float> locs) {
    offset_ = 0.0f;
    sciExp_ = 0;
    useSci_ = false;
    offsetText_.clear();
    if (locs.empty()) return;

    float maxAbs = 0.0f, lo = locs.front(), hi = locs.front();
    for (float v : locs) {
        maxAbs = std::max(maxAbs, std::abs(v));
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (maxAbs <= 0.0f) return;
    int oom = static_cast<int>(std::floor(std::log10(maxAbs)));

    // Scientific notation: order of magnitude outside scilimits.
    if (forceSci || oom < scilimits.first || oom >= scilimits.second) {
        useSci_ = true;
        sciExp_ = oom;
        offsetText_ = useMathText
            ? std::format("$10^{{{}}}$", sciExp_)
            : std::format("1e{}", sciExp_);
        return;
    }

    // Offset mode (matplotlib): when the ticks cluster around a large
    // value, subtract the offset and show "+offset" at the axis end.
    if (useOffset && locs.size() > 1) {
        float mean = std::accumulate(locs.begin(), locs.end(), 0.0f) /
                     static_cast<float>(locs.size());
        float range = hi - lo;
        if (std::abs(mean) > 0.0f) {
            int om = static_cast<int>(std::floor(std::log10(std::abs(mean))));
            // Cluster: range is ≥2 decades smaller than the offset.
            if (range < std::pow(10.0f, om - 1) &&
                om >= scilimits.first && om < scilimits.second) {
                offset_ = std::floor(mean / std::pow(10.0f, om - 1)) *
                          std::pow(10.0f, om - 1);
                offsetText_ = useMathText
                    ? std::format("${:+g}$", offset_)
                    : std::format("{:+g}", offset_);
            }
        }
    }
}

std::string ScalarFormatter::format(float v, int) const {
    float val = v - offset_;
    if (useSci_) val = v / std::pow(10.0f, static_cast<float>(sciExp_));
    if (val == 0.0f) val = std::abs(val); // -0 → 0
    return gFormat(val);
}

// ─── Log formatters ─────────────────────────────────────────────────────────

std::string LogFormatter::format(float v, int) const {
    if (auto e = exactPower(v, base_))
        return std::format("$10^{{{}}}$", *e);
    return gFormat(v);
}

std::string LogFormatterExponent::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    int e = static_cast<int>(std::round(std::log(v) / std::log(base_)));
    return std::format("$10^{{{}}}$", e);
}

std::string LogFormatterMathtext::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    int e = static_cast<int>(std::round(std::log(v) / std::log(base_)));
    return std::format("${:g}^{{{}}}$", base_, e);
}

std::string LogFormatterSciNotation::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    int e = static_cast<int>(std::floor(std::log(v) / std::log(base_)));
    float m = v / std::pow(base_, static_cast<float>(e));
    if (std::abs(m - std::round(m)) < 1e-4f)
        return std::format("${}\\times10^{{{}}}$", int(std::round(m)), e);
    return std::format("${:.3g}\\times10^{{{}}}$", m, e);
}

std::string LogitFormatter::format(float v, int) const {
    if (v <= 0.0f || v >= 1.0f) return {};
    if (v >= 0.01f && v <= 0.99f) {
        return trimZeros(std::format("{:.2f}", v));
    }
    return gFormat(v);
}

std::string EngFormatter::format(float v, int) const {
    static const char* kPrefix[] = {"y","z","a","f","p","n","µ","m","",
                                    "k","M","G","T","P","E","Z","Y"};
    if (v == 0.0f)
        return std::format("0{}{}", unit_.empty() ? "" : sep_, unit_);
    int exp3 = static_cast<int>(std::floor(std::log10(std::abs(v)) / 3.0f));
    exp3 = std::clamp(exp3, -8, 8);
    float mant = v / std::pow(1000.0f, static_cast<float>(exp3));
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", places_, double(mant));
    std::string s = trimZeros(buf);
    const char* pfx = kPrefix[exp3 + 8];
    if (*pfx == '\0' && unit_.empty()) return s;
    return std::format("{}{}{}{}", s, sep_, pfx, unit_);
}

std::string PercentFormatter::format(float v, int) const {
    float pct = v / xmax_ * 100.0f;
    int dec = decimals_ >= 0 ? decimals_
        : (std::abs(pct - std::round(pct)) < 1e-4f ? 0 : 1);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", dec, double(pct));
    return std::format("{}{}", buf, symbol_);
}

} // namespace volcano::plot
