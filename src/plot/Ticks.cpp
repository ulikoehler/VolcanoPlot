// volcano/plot/Ticks.cpp — locator/formatter implementations
#include "volcano/plot/Ticks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <numeric>
#include <set>

namespace volcano::plot {

namespace {

/// matplotlib ticker.scale_range: a decade scale for the step staircase
/// and, when ticks cluster far from zero, a large constant offset that
/// is subtracted before locating ticks (precision fixup).
std::pair<double, double> scaleRange(double vmin, double vmax, double n) {
    double dv = std::abs(vmax - vmin);
    if (!(dv > 0)) dv = 1;  // mpl nonsingular guarantees dv > 0 upstream
    double meanv = (vmax + vmin) / 2.0;
    double offset = std::abs(meanv) / dv < 100.0
        ? 0.0
        : std::copysign(
              std::pow(10.0, std::floor(std::log10(std::abs(meanv)))),
              meanv);
    double scale = std::pow(10.0, std::floor(std::log10(dv / n)));
    return {scale, offset};
}

/// matplotlib ticker._Edge_integer: integer-multiple comparisons of
/// x/step with a tolerance widened when a large offset is in play.
struct EdgeInteger {
    double step, offset;
    /// mpl _Edge_integer.closeto, widened by the f32 quantization error
    /// of x (our data model is float32, mpl computes in float64).
    [[nodiscard]] bool closeto(double ms, double edge, double x) const {
        double tol = std::max(1e-10, std::abs(x) / step * 1.5e-7);
        if (offset > 0) {
            double digits = std::log10(offset / step);
            tol = std::max(tol, std::pow(10.0, digits - 12));
        }
        tol = std::min(0.4999, tol);
        return std::abs(ms - edge) < tol;
    }
    /// Largest n such that n·step <= x.
    [[nodiscard]] double le(double x) const {
        double d = std::floor(x / step);
        double m = x - d * step;
        if (closeto(m / step, 1.0, x)) return d + 1;
        return d;
    }
    /// Smallest n such that n·step >= x.
    [[nodiscard]] double ge(double x) const {
        double d = std::floor(x / step);
        double m = x - d * step;
        if (closeto(m / step, 0.0, x)) return d;
        return d + 1;
    }
};

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

/// Recover the float64 value a tick position was meant to represent.
/// Our data model is float32; a value like 0.005f quantizes to
/// 0.004999999888, which then formats differently than mpl's f64
/// 0.005 ("0.00" vs "0.01"). float32 carries ~7 significant decimal
/// digits — anything beyond is quantization noise, so %.7g + strtod
/// snaps back to the intended value.
double snapF64(float v) {
    if (!std::isfinite(v)) return v;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.7g", double(v));
    return std::strtod(buf, nullptr);
}

/// mpl Formatter.fix_minus (axes.unicode_minus=True default): ASCII
/// '-' → U+2212. Kept in sync with render::fixMinus.
std::string fixMinusStr(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c == '-') out += "\xe2\x88\x92";
        else out += c;
    return out;
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
    // mpl FixedLocator returns all locs regardless of the view
    // interval; out-of-view ticks are clipped at draw time.
    (void)vmin; (void)vmax;
    return positions_;
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
    // mpl MultipleLocator.tick_values: edge-align vmin, then emit one
    // step below it through at least one step above vmax (ticks may
    // extend beyond the view interval; drawing clips them).
    std::vector<float> out;
    double step = base_;
    if (!(step > 0.0) || !std::isfinite(step)) return out;
    if (vmax < vmin) std::swap(vmin, vmax);
    double lo = double(vmin) - offset_, hi = double(vmax) - offset_;
    EdgeInteger edge{step, std::abs(offset_)};
    lo = edge.ge(lo) * step;
    double n = std::floor((hi - lo) / step + 0.001);
    if (!(n >= 0) || n > 100000) return out;  // density guard
    for (double i = 0; i <= n + 2; ++i)
        out.push_back(float(lo + (i - 1.0) * step + offset_));
    return out;
}

std::vector<float> IndexLocator::tickValues(float vmin, float vmax) const {
    // mpl IndexLocator.tick_values: arange(vmin + offset, vmax + 1,
    // base) — ticks start at vmin+offset, not aligned to base.
    std::vector<float> out;
    double base = base_;
    if (!(base > 0.0) || !std::isfinite(base)) return out;
    if (vmax < vmin) std::swap(vmin, vmax);
    for (double v = double(vmin) + offset_; v < double(vmax) + 1.0;
         v += base) {
        out.push_back(float(v));
        if (out.size() > 100000) break;
    }
    return out;
}

void MaxNLocator::setSteps(std::vector<float> steps) {
    // mpl validates 1 <= s <= 10, strictly increasing, needs >= 2 entries
    // (the staircase uses steps[1] for the top extension).
    if (steps.size() < 2) return;
    std::ranges::sort(steps);
    if (steps.front() < 1.0f || steps.back() > 10.0f) return;
    steps_ = std::move(steps);
}

void MaxNLocator::setPrune(std::string_view p) {
    prune_ = (p == "lower" || p == "upper" || p == "both")
        ? std::string(p) : std::string{};
}

std::vector<float> MaxNLocator::tickValues(float vmin, float vmax) const {
    double dmin = vmin, dmax = vmax;
    if (symmetric_) {
        dmax = std::max(std::abs(dmin), std::abs(dmax));
        dmin = -dmax;
    }
    // mpl transforms.nonsingular(expander=1e-13, tiny=1e-14).
    if (!std::isfinite(dmin) || !std::isfinite(dmax)) {
        dmin = -1e-13; dmax = 1e-13;
    }
    if (dmax < dmin) std::swap(dmin, dmax);
    double maxabs = std::max(std::abs(dmin), std::abs(dmax));
    constexpr double kTiny = 1e-14, kExpander = 1e-13;
    if (maxabs < 1e6 / kTiny * std::numeric_limits<double>::denorm_min() ||
        maxabs == 0.0) {
        dmin = -kExpander; dmax = kExpander;
    } else if (dmax - dmin <= maxabs * kTiny) {
        dmin -= kExpander * std::abs(dmin);
        dmax += kExpander * std::abs(dmax);
    }

    auto locs = rawTicks(dmin, dmax);
    if (prune_ == "lower" && !locs.empty()) locs.erase(locs.begin());
    else if (prune_ == "upper" && !locs.empty()) locs.pop_back();
    else if (prune_ == "both" && locs.size() >= 2) {
        locs.erase(locs.begin());
        locs.pop_back();
    }
    return locs;
}

std::vector<float> MaxNLocator::rawTicks(double vmin, double vmax) const {
    // mpl: nbins='auto' without an axis resolves to 9; axisTicks
    // substitutes the tick-space-derived nbins for attached locators.
    int nbins = nbins_ <= 0 ? 9 : nbins_;
    auto [scale, offset] = scaleRange(vmin, vmax, double(nbins));
    double vminS = vmin - offset, vmaxS = vmax - offset;

    // mpl _extended_steps = concat(0.1*steps[:-1], steps, [10*steps[1]]).
    std::vector<double> steps;
    steps.reserve(steps_.size() * 2);
    for (size_t i = 0; i + 1 < steps_.size(); ++i)
        steps.push_back(0.1 * double(steps_[i]));
    for (float s : steps_) steps.push_back(s);
    if (steps_.size() > 1) steps.push_back(10.0 * steps_[1]);
    for (auto& s : steps) s *= scale;
    if (integer_)
        std::erase_if(steps, [](double s) {
            // mpl: for steps > 1 keep only integer values.
            return s >= 1.0 && std::abs(s - std::round(s)) >= 0.001;
        });
    if (steps.empty()) steps.push_back(scale > 0 ? scale : 1.0);

    double rawStep = (vmaxS - vminS) / nbins;
    // mpl: smallest step >= raw_step, else the largest step.
    size_t istep = steps.size() - 1;
    for (size_t i = 0; i < steps.size(); ++i)
        if (steps[i] >= rawStep) { istep = i; break; }

    // Try steps from istep down until one emits >= min_n_ticks ticks.
    std::vector<float> ticks;
    for (size_t k = istep + 1; k-- > 0;) {
        double step = steps[k];
        if (integer_ &&
            std::floor(vmaxS) - std::ceil(vminS) >= minNTicks_ - 1)
            step = std::max(1.0, step);
        double bestVmin = std::floor(vminS / step) * step;
        EdgeInteger edge{step, std::abs(offset)};
        double low = edge.le(vminS - bestVmin);
        double high = edge.ge(vmaxS - bestVmin);
        // Pathological density guard (e.g. step below the ulp of the
        // bounds): mpl relies on raise_if_exceeds; bail out instead.
        if (!(high - low < 100000)) continue;
        ticks.clear();
        int nticks = 0;
        for (double n = low; n <= high; n += 1.0) {
            double t = n * step + bestVmin;
            if (t >= vminS && t <= vmaxS) ++nticks;
            ticks.push_back(static_cast<float>(t));
        }
        if (nticks >= minNTicks_) break;
    }
    for (auto& t : ticks) t = static_cast<float>(double(t) + offset);
    return ticks;
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
        // matplotlib AutoMinorLocator: 5 subdivisions when the major
        // step's mantissa divides 5 (1, 2.5, 5 × 10^k), else 4.
        int nd = ndivs_ > 0 ? ndivs_ : 4;
        if (ndivs_ == 0) {
            float exp10 = std::floor(std::log10(step));
            float mant = step / std::pow(10.0f, exp10);
            if (std::abs(std::fmod(5.0f, mant)) < 1e-4f) nd = 5;
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

std::vector<float> AsinhLocator::tickValues(float vmin, float vmax) const {
    // matplotlib AsinhLocator.__call__ + tick_values (mpl 3.10):
    // almost-symmetric ranges snap to exactly symmetric bounds.
    if (vmin * vmax < 0.0f &&
        std::abs(1.0 + double(vmax) / double(vmin)) < symthresh_) {
        float bound = std::max(std::abs(vmin), std::abs(vmax));
        vmin = -bound;
        vmax = bound;
    }
    const double lw = linearWidth_;
    double ymin = lw * std::asinh(double(vmin) / lw);
    double ymax = lw * std::asinh(double(vmax) / lw);
    int n = std::max(numticks_, 2);
    std::vector<double> ys;
    ys.reserve(size_t(n) + 1);
    for (int i = 0; i < n; ++i)
        ys.push_back(ymin + (ymax - ymin) * double(i) / double(n - 1));
    if (ymin * ymax < 0) {
        // Straddling zero: drop near-zero ticks and add an exact 0
        // (mpl keeps ticks whose relative deviation > 0.5/numticks).
        std::vector<double> keep;
        for (double y : ys)
            if (std::abs(y / (ymax - ymin)) > 0.5 / double(n))
                keep.push_back(y);
        keep.push_back(0.0);
        ys = std::move(keep);
    }
    std::set<double> ticks;
    for (double y : ys) {
        double x = lw * std::sinh(y / lw);
        if (base_ > 1) {
            // pows = sign(x) * base^floor(log_base|x|) — x==0 → 0.
            // Compute in double: log(10.0f) rounding can push the
            // ratio just below an integer boundary (100 → pows 10).
            const double base = base_;
            double p = x == 0.0
                           ? 0.0
                           : std::copysign(
                                 std::pow(base, std::floor(
                                                    std::log(std::abs(x)) /
                                                    std::log(base))),
                                 x);
            if (subs_.empty()) {
                ticks.insert(p);
            } else {
                for (double s : subs_) ticks.insert(p * s);
            }
        } else {
            double p = x == 0.0 ? 1.0
                                : std::pow(10.0, std::floor(
                                                     std::log10(
                                                         std::abs(x))));
            ticks.insert(p * std::round(x / p));
        }
    }
    if (ticks.size() >= 2)
        return std::vector<float>(ticks.begin(), ticks.end());
    std::vector<float> out;
    out.resize(size_t(n));
    for (int i = 0; i < n; ++i)
        out[size_t(i)] = vmin + (vmax - vmin) * float(i) / float(n - 1);
    return out;
}

// ─── Formatters ─────────────────────────────────────────────────────────────

std::string FixedFormatter::format(float, int pos) const {
    if (pos < 0 || static_cast<size_t>(pos) >= labels_.size()) return {};
    return labels_[static_cast<size_t>(pos)];
}

std::string FormatStrFormatter::format(float v, int) const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), fmt_.c_str(), snapF64(v));
    return buf;
}

/// Format `v` per a Python str.format spec ('{x:.2f}' → spec ".2f").
/// Supports [[fill]align][sign][0][width][.prec][type] for numeric
/// types f/F/e/E/g/G/d/% plus empty spec (gFormat for floats).
static std::string pyFormatSpec(double v, std::string_view spec,
                                bool isInt) {
    size_t i = 0, n = spec.size();
    char fill = ' ', align = 0;
    if (i + 1 < n && (spec[i + 1] == '<' || spec[i + 1] == '>' ||
                      spec[i + 1] == '^' || spec[i + 1] == '=')) {
        fill = spec[i]; align = spec[i + 1]; i += 2;
    } else if (i < n && (spec[i] == '<' || spec[i] == '>' ||
                         spec[i] == '^' || spec[i] == '=')) {
        align = spec[i]; ++i;
    }
    char sign = 0;
    if (i < n && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' '))
        sign = spec[i++];
    bool zeroPad = false;
    if (i < n && spec[i] == '0') { zeroPad = true; ++i; }
    int width = 0;
    while (i < n && std::isdigit(static_cast<unsigned char>(spec[i])))
        width = width * 10 + (spec[i++] - '0');
    int prec = -1;
    if (i < n && spec[i] == '.') {
        ++i; prec = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(spec[i])))
            prec = prec * 10 + (spec[i++] - '0');
    }
    char type = i < n ? spec[i] : '\0';

    // printf handles width for '<' (left) and plain '>' (right); '^',
    // '=' and custom fill chars are padded manually below.
    const bool manualPad = align == '^' || align == '=' ||
                           (align != 0 && fill != ' ') ||
                           (align != 0 && zeroPad);
    // Build a printf pattern for the numeric core.
    std::string pat = "%";
    if (align == '<' && !manualPad) pat += '-';
    if (sign) pat += sign;
    if (zeroPad && !manualPad) pat += '0';
    if (width > 0 && !manualPad) pat += std::to_string(width);
    if (prec >= 0) { pat += '.'; pat += std::to_string(prec); }
    char buf[128];
    bool appendPct = false;
    switch (type) {
    case 'd': case 'i': case 'n': case '\0':
        if (type == '\0' && !isInt) {
            pat += "g";
            std::snprintf(buf, sizeof(buf), pat.c_str(), v);
        } else {
            if (type == 'n') pat += "d"; else pat += "lld";
            std::snprintf(buf, sizeof(buf), pat.c_str(),
                          (long long)std::llround(v));
        }
        break;
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        pat += type == 'F' ? 'f' : type;
        std::snprintf(buf, sizeof(buf), pat.c_str(), v);
        break;
    case '%':
        pat += 'f'; appendPct = true;
        std::snprintf(buf, sizeof(buf), pat.c_str(), v * 100.0);
        break;
    default:
        pat += 'g';
        std::snprintf(buf, sizeof(buf), pat.c_str(), v);
        break;
    }
    std::string out = buf;
    if (appendPct) out += '%';
    // fill/align for the cases printf can't express ('^', '=', custom
    // fill); '^' puts the extra pad on the right like Python.
    if (manualPad && int(out.size()) < width) {
        int pad = width - int(out.size());
        if (align == '^') {
            int l = pad / 2;
            out = std::string(l, fill) + out + std::string(pad - l, fill);
        } else if (align == '<') {
            out += std::string(pad, fill);
        } else if (align == '=' &&
                   (out.starts_with('-') || out.starts_with('+') ||
                    out.starts_with(' '))) {
            out = out.substr(0, 1) + std::string(pad, fill) +
                  out.substr(1);
        } else {
            out = std::string(pad, fill) + out;
        }
    }
    return out;
}

std::string StrMethodFormatter::format(float v, int pos) const {
    // mpl StrMethodFormatter: tmpl.format(x=v, pos=pos) — substitute
    // {x}, {pos} and their '{name:spec}' forms.
    std::string out;
    size_t i = 0;
    while (i < tmpl_.size()) {
        char c = tmpl_[i];
        if (c == '{') {
            if (i + 1 < tmpl_.size() && tmpl_[i + 1] == '{') {
                out += '{'; i += 2; continue;
            }
            auto close = tmpl_.find('}', i);
            if (close == std::string::npos) { out += c; ++i; continue; }
            std::string_view inner(&tmpl_[i + 1], close - i - 1);
            auto colon = inner.find(':');
            std::string_view name = inner.substr(0, colon);
            std::string_view spec =
                colon == std::string_view::npos ? "" : inner.substr(colon + 1);
            if (name == "x")
                // mpl _UnicodeMinusFormat applies fix_minus to the
                // substituted value (template literals keep '-').
                out += fixMinusStr(
                    spec.empty() ? gFormat(v)
                                 : pyFormatSpec(snapF64(v), spec, false));
            else if (name == "pos")
                out += spec.empty() ? std::to_string(pos)
                                    : pyFormatSpec(pos, spec, true);
            else
                out += "{" + std::string(inner) + "}";
            i = close + 1;
        } else if (c == '}' && i + 1 < tmpl_.size() &&
                   tmpl_[i + 1] == '}') {
            out += '}'; i += 2;
        } else {
            out += c; ++i;
        }
    }
    return out;
}

// ─── ScalarFormatter ────────────────────────────────────────────────────────

void ScalarFormatter::setLocs(std::span<const float> locs) {
    offset_ = 0.0f;
    sciExp_ = 0;
    decimals_ = -1;
    useSci_ = false;
    offsetText_.clear();
    if (locs.empty()) return;

    // Common decimal precision (matplotlib ScalarFormatter._set_format):
    // enough decimals to represent every tick exactly → uniform labels
    // like "1.0, 0.5, 0.0" instead of "1, 0.5, 0".
    {
        int d = 0;
        for (; d < 6; ++d) {
            float m = std::pow(10.0f, d);
            bool allInt = true;
            for (float v : locs) {
                float r = v * m;
                if (std::abs(r - std::round(r)) > 1e-4f * std::max(1.0f, std::abs(r))) { allInt = false; break; }
            }
            if (allInt) break;
        }
        decimals_ = (d < 6) ? d : -1;
    }

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
    // Uniform precision in plain mode (matplotlib pads all labels to the
    // same decimals, e.g. "1.0, 0.5, 0.0").
    if (!useSci_ && decimals_ > 0)
        return std::format("{:.{}f}", val, decimals_);
    return gFormat(val);
}

// ─── Log formatters ─────────────────────────────────────────────────────────

void LogFormatter::setViewInterval(float vmin, float vmax) {
    if (vmin > vmax) std::swap(vmin, vmax);
    // mpl: non-positive interval (e.g. a colorbar) labels only powers.
    if (vmin <= 0.0f) {
        sublabels_ = std::vector<int>{1};
        return;
    }
    float b = base_;
    float numdec = std::abs(std::log(vmax) / std::log(b) -
                            std::log(vmin) / std::log(b));
    if (numdec > minorThresholds.first) {
        sublabels_ = std::vector<int>{1};  // label only bases
    } else if (numdec > minorThresholds.second) {
        // mpl: geomspace(1, b, b//2 + 1) rounded — base 10 gives
        // {1, 2, 3, 4, 6, 10}.
        int n = static_cast<int>(b) / 2 + 1;
        std::vector<int> s;
        for (int i = 0; i < n; ++i)
            s.push_back(static_cast<int>(std::lround(
                std::pow(b, double(i) / (n - 1)))));
        std::ranges::sort(s);
        s.erase(std::unique(s.begin(), s.end()), s.end());
        sublabels_ = std::move(s);
    } else {
        std::vector<int> s;
        for (int i = 1; i <= static_cast<int>(b); ++i) s.push_back(i);
        sublabels_ = std::move(s);
    }
}

bool LogFormatter::passesSublabels(float v) const {
    if (v <= 0.0f) return true;  // let the subclass format it
    float fx = std::log(v) / std::log(base_);
    bool isDecade = std::abs(fx - std::round(fx)) < 1e-6f;
    float e = isDecade ? std::round(fx) : std::floor(fx);
    int coeff = static_cast<int>(
        std::lround(std::pow(base_, double(fx - e))));
    if (labelOnlyBase && !isDecade) return false;
    if (sublabels_ &&
        std::ranges::find(*sublabels_, coeff) == sublabels_->end())
        return false;
    return true;
}

std::string LogFormatter::format(float v, int) const {
    if (!passesSublabels(v)) return {};
    if (auto e = exactPower(v, base_))
        return std::format("$10^{{{}}}$", *e);
    return gFormat(v);
}

std::string LogFormatterExponent::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    if (!passesSublabels(v)) return {};
    int e = static_cast<int>(std::round(std::log(v) / std::log(base_)));
    return std::format("$10^{{{}}}$", e);
}

std::string LogFormatterMathtext::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    if (!passesSublabels(v)) return {};
    int e = static_cast<int>(std::round(std::log(v) / std::log(base_)));
    return std::format("${:g}^{{{}}}$", base_, e);
}

std::string LogFormatterSciNotation::format(float v, int) const {
    if (v <= 0.0f) return gFormat(v);
    if (!passesSublabels(v)) return {};
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
    // mpl EngFormatter.format_data: places=None → "%g" mantissa,
    // else "%.{places}f"; mantissas rounding up to 1000 roll over to
    // the next prefix.
    static const char* kPrefix[] = {"y","z","a","f","p","n","µ","m","",
                                    "k","M","G","T","P","E","Z","Y"};
    double value = snapF64(v);
    // mpl: pow10=0 for value 0, but the mantissa still goes through
    // the fmt string ('0.00' for places=2, '0' for 'g').
    int pow10 = value != 0.0
        ? int(std::floor(std::log10(std::abs(value)) / 3.0) * 3) : 0;
    pow10 = std::clamp(pow10, -24, 24);
    double mant = value / std::pow(10.0, pow10);
    char buf[64];
    auto fmtMant = [&](double m) {
        if (places_ < 0) return gFormat(float(m));
        std::snprintf(buf, sizeof(buf), "%.*f", places_, m);
        return std::string(buf);
    };
    // mpl: if the formatted mantissa rounds up to 1000, bump a decade.
    if (std::abs(std::atof(fmtMant(mant).c_str())) >= 1000.0 &&
        pow10 < 24) {
        mant /= 1000.0;
        pow10 += 3;
    }
    std::string s = fmtMant(mant);
    const char* pfx = kPrefix[pow10 / 3 + 8];
    if (*pfx == '\0' && unit_.empty()) return s;
    return std::format("{}{}{}{}", s, sep_, pfx, unit_);
}

std::string PercentFormatter::format(float v, int) const {
    // mpl PercentFormatter: pct = x*100/xmax; decimals=None →
    // clamp(ceil(2 - log10(2*scaled_range)), 0, 5) where scaled_range
    // is the display range in percent units.
    double pct = snapF64(v) * 100.0 / xmax_;
    int dec;
    if (decimals_ >= 0) {
        dec = decimals_;
    } else {
        double scaledRange = std::abs(double(vmax_) - vmin_) * 100.0 /
                             xmax_;
        // f32 view bounds perturb the log by ~1e-7; mpl computes in
        // f64 and lands on exact integers at decade boundaries, so
        // shave a small epsilon off before ceil.
        dec = scaledRange <= 0
                  ? 0
                  : std::clamp(int(std::ceil(
                                   2.0 - std::log10(2.0 * scaledRange) -
                                   1e-6)),
                               0, 5);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", dec, pct);
    return std::format("{}{}", buf, symbol_);
}

} // namespace volcano::plot
