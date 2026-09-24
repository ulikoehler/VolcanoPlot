// src/render/TickLayout.cpp — tick/layout helpers shared between the
// raster Renderer and the VectorRenderer driver.
#include "TickLayout.hpp"

#include "volcano/plot/Normalize.hpp"

#include <cmath>
#include <format>

namespace volcano::render {

/// Nice-number tick locator (matplotlib _ColorbarAutoLocator — an
/// AutoLocator with the [1,2,2.5,5,10] staircase; used for colorbar
/// ticks). Returns edge-inclusive positions spanning [vmin, vmax].
std::vector<float> autoTicks(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return {};
    plot::MaxNLocator loc{nbins};
    loc.setSteps({1.0f, 2.0f, 2.5f, 5.0f, 10.0f});
    return loc.tickValues(vmin, vmax);
}

/// Format a tick value as a short string, using the step size to determine
/// the appropriate number of decimal places (matching matplotlib's ScalarFormatter).
std::string formatTick(float v, float step) {
    // Normalize -0.0f to 0.0f to avoid "-0.0" in output.
    if (v == 0.0f) v = std::abs(v);
    // Very large or very small ranges use scientific notation.
    if (std::abs(v) >= 10000.0f || (std::abs(v) < 0.001f && step < 0.001f)) {
        return std::format("{:.1e}", v);
    }
    // Determine decimal places from the step size.
    // Use enough decimal places to represent the step exactly.
    // e.g., step=0.5 → 1 dp, step=0.25 → 2 dp, step=0.1 → 1 dp.
    float stepMag = std::abs(step);
    if (stepMag >= 1.0f) {
        // Integer steps: no decimal places.
        return std::format("{:.0f}", v);
    }
    // Find the minimum decimals where round(step, d) == step.
    int decimals = 0;
    while (decimals < 6) {
        float scale = std::pow(10.0f, decimals);
        float rounded = std::round(stepMag * scale) / scale;
        if (std::abs(rounded - stepMag) < stepMag * 0.01f) break;
        ++decimals;
    }
    return std::format("{:.{}f}", v, decimals);
}

/// Compute the nice step size used by autoTicks.
float autoTickStep(float vmin, float vmax, int nbins) {
    auto t = autoTicks(vmin, vmax, nbins);
    if (t.size() < 2) return 1.0f;
    float step = t[1] - t[0];
    return (std::isfinite(step) && step > 0.0f) ? step : 1.0f;
}

// ─── Locator/formatter plumbing (matplotlib ticker framework) ───────────────

/// Major tick positions honoring TickConfig locator/positions overrides.
std::vector<float> axisTicks(const plot::TickConfig& tc,
                             const plot::AxisScale& scale,
                             float lo, float hi,
                             float axisLengthPx,
                             float fontPt, float dpi, bool yAxis) {
    // mpl Axis.get_tick_space: estimated labels that fit along the
    // axis. X assumes ≤3:1 text aspect, Y uses 2× line spacing.
    auto tickSpace = [&]() -> int {
        if (axisLengthPx <= 0.0f) return -1;
        float lengthPt = axisLengthPx * 72.0f / std::max(dpi, 1.0f);
        float labelPt = std::max(fontPt, 1.0f) * (yAxis ? 2.0f : 3.0f);
        return int(std::floor(lengthPt / labelPt));
    };
    if (tc.locator) {
        // mpl AutoLocator = MaxNLocator with nbins='auto': nbins =
        // clip(get_tick_space(), max(1, min_n_ticks-1), 9).
        if (auto* mnl = dynamic_cast<plot::MaxNLocator*>(tc.locator.get());
            mnl && mnl->autoNbins()) {
            plot::MaxNLocator copy = *mnl;
            int space = tickSpace();
            copy.setNbins(space < 0 ? 9
                          : std::clamp(space,
                                       std::max(1, mnl->minNTicks() - 1),
                                       9));
            return copy.tickValues(lo, hi);
        }
        return tc.locator->tickValues(lo, hi);
    }
    // An explicitly-set positions list is honored even when empty
    // (matplotlib ax.set_xticks([]) hides the ticks entirely).
    if (tc.positions) return *tc.positions;
    // mpl's default axis locator is AutoLocator: nbins=clip(space,…,9)
    // with steps [1,2,2.5,5,10].
    int space = tickSpace();
    int nb = space < 0 ? 9 : std::clamp(space, 1, 9);
    return plot::scaleTicks(scale, lo, hi, nb);
}

/// Log-family scales get automatic minor ticks (matplotlib behavior).
static bool isLogish(plot::ScaleKind k) {
    using SK = plot::ScaleKind;
    return k == SK::Log || k == SK::Symlog || k == SK::Logit ||
           k == SK::Asinh || k == SK::FunctionLog;
}

/// Whether minor tick marks are drawn for this axis.
bool minorEnabled(const plot::TickConfig& tc, const plot::AxisScale& s) {
    return tc.minor || tc.minorLocator != nullptr ||
           s.kind == plot::ScaleKind::Log || s.kind == plot::ScaleKind::Logit;
}

/// Minor tick positions: explicit minorLocator, log-family subs, or
/// linear subdivisions between majors (AutoMinorLocator).
std::vector<float> axisMinorTicks(const plot::TickConfig& tc,
                                  const plot::AxisScale& scale,
                                  float lo, float hi,
                                  std::span<const float> majors) {
    if (!minorEnabled(tc, scale)) return {};
    if (tc.minorLocator) return tc.minorLocator->tickValues(lo, hi);
    if (scale.kind == plot::ScaleKind::Log) {
        float base = scale.param1 > 0.0f ? scale.param1 : 10.0f;
        return plot::LogLocator{base}.minorValues(lo, hi);
    }
    if (scale.kind == plot::ScaleKind::FunctionLog) {
        float base = scale.param1 > 0.0f ? scale.param1 : 10.0f;
        return plot::LogLocator{base}.minorValues(lo, hi);
    }
    if (isLogish(scale.kind)) return {};
    return plot::AutoMinorLocator{}.between(majors, lo, hi);
}

/// Formatter for an axis: explicit formatter, legacy format string, or
/// default ScalarFormatter (setLocs gives offset/scientific detection).
plot::Formatter* axisFormatter(const plot::TickConfig& tc,
                               std::span<const float> ticks,
                               const plot::FigureStyle& style,
                               const plot::AxisScale& scale,
                               float vmin, float vmax,
                               plot::ScalarFormatter& defaultFmt,
                               plot::FormatStrFormatter& strFmt,
                               plot::LogFormatterMathtext& logFmt) {
    plot::Formatter* f = tc.formatter.get();
    if (!f && tc.format != "%g") {
        strFmt = plot::FormatStrFormatter(tc.format);
        f = &strFmt;
    }
    // mpl log axes default to LogFormatterSciNotation ($m×10^{k}$);
    // symlog axes use the same formatter against their base (param3).
    if (!f && (scale.kind == plot::ScaleKind::Log ||
               scale.kind == plot::ScaleKind::FunctionLog ||
               scale.kind == plot::ScaleKind::Symlog)) {
        float base = scale.kind == plot::ScaleKind::Symlog
                         ? (scale.param3 > 0.0f ? scale.param3 : 10.0f)
                         : (scale.param1 > 0.0f ? scale.param1 : 10.0f);
        logFmt = plot::LogFormatterMathtext{base};
        f = &logFmt;
    }
    if (!f) {
        defaultFmt = plot::ScalarFormatter{};
        defaultFmt.scilimits = style.formatterLimits;
        defaultFmt.useOffset = style.formatterUseOffset;
        defaultFmt.useMathText = style.formatterUseMathText;
        f = &defaultFmt;
    }
    f->setViewInterval(vmin, vmax);
    f->setLocs(ticks);
    return f;
}

/// mpl log axes get a default minor formatter (LogFormatterSciNotation)
/// that labels minor subs; suppression of crowded cases happens inside
/// the formatter via minor_thresholds.
bool logMinorLabels(const plot::TickConfig& tc,
                    const plot::AxisScale& scale) {
    if (tc.minorFormatter) return true;
    return minorEnabled(tc, scale) &&
           (scale.kind == plot::ScaleKind::Log ||
            scale.kind == plot::ScaleKind::FunctionLog);
}

/// Label for tick `t` at index `i`, honoring fixed labels first.
std::string tickLabel(const plot::TickConfig& tc, const plot::Formatter& f,
                      float t, int i) {
    if (!tc.formatter && tc.positions && tc.labels &&
        tc.positions->size() == tc.labels->size()) {
        for (size_t k = 0; k < tc.positions->size(); ++k)
            if (std::abs((*tc.positions)[k] - t) < 1e-6f)
                // mpl set_ticks labels are user text, returned raw.
                return (*tc.labels)[k];
    }
    return fmtLabel(f, t, i);
}

std::string fixMinus(std::string s) {
    // U+2212 '−' in UTF-8 is 0xE2 0x88 0x92.
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c == '-') out += "\xe2\x88\x92";
        else out += c;
    return out;
}

/// TickConfig direction string → fraction of tick length inside the axes.
float tickInFrac(const plot::TickConfig& tc) {
    if (tc.direction == "in") return 1.0f;
    if (tc.direction == "inout") return 0.5f;
    return 0.0f; // "out"
}

ColorbarTickSet colorbarTicks(std::span<const float> explicitTicks,
                              std::span<const std::string> explicitLabels,
                              bool minorTicksOn,
                              const plot::Normalize* norm,
                              float vmin, float vmax,
                              std::string_view fmt) {
    ColorbarTickSet out;
    const float linStep = autoTickStep(vmin, vmax, 8);
    // mpl `format=` — printf-style FormatStrFormatter; falls back to
    // formatTick when the spec is malformed.
    auto fmtOr = [&](float v, const std::string& fallback) {
        if (fmt.empty()) return fallback;
        char buf[128];
        std::string f{fmt};
        int n = std::snprintf(buf, sizeof buf, f.c_str(),
                              static_cast<double>(v));
        return n > 0 ? std::string(buf, size_t(n)) : fallback;
    };
    if (!explicitTicks.empty()) {
        out.majors.assign(explicitTicks.begin(), explicitTicks.end());
        for (size_t i = 0; i < out.majors.size(); ++i)
            out.labels.push_back(i < explicitLabels.size()
                ? std::string(explicitLabels[i])
                : fmtOr(out.majors[i],
                        formatTick(out.majors[i], linStep)));
    } else if (dynamic_cast<const plot::LogNorm*>(norm) && vmin > 0.0f) {
        // mpl: LogNorm colorbars put the long axis on log scale →
        // LogLocator decades + LogFormatterSciNotation + subs minors.
        plot::LogLocator loc;
        out.majors = loc.tickValues(vmin, vmax);
        plot::LogFormatter f;
        f.setViewInterval(vmin, vmax);
        f.setLocs(out.majors);
        for (size_t i = 0; i < out.majors.size(); ++i)
            out.labels.push_back(
                fmtOr(out.majors[i],
                      fmtLabel(f, out.majors[i], int(i))));
        out.minors = loc.minorValues(vmin, vmax);
    } else if (auto* sln = dynamic_cast<const plot::SymLogNorm*>(norm)) {
        // mpl: SymLogNorm colorbars get SymmetricalLogLocator ticks.
        out.majors = plot::SymmetricalLogLocator{sln->linthresh()}
                         .tickValues(vmin, vmax);
        plot::LogFormatter f;
        f.setViewInterval(vmin, vmax);
        f.setLocs(out.majors);
        for (size_t i = 0; i < out.majors.size(); ++i) {
            std::string s = fmtLabel(f, out.majors[i], int(i));
            out.labels.push_back(
                fmtOr(out.majors[i],
                      s.empty() ? formatTick(out.majors[i], linStep)
                                : s));
        }
    } else {
        out.majors = autoTicks(vmin, vmax, 8);
        for (float v : out.majors)
            out.labels.push_back(
                fmtOr(v, formatTick(v, linStep)));
    }
    // mpl AutoMinorLocator-style minor marks between linear majors;
    // log minors come from the locator's subs above.
    if (out.minors.empty() && minorTicksOn) {
        for (size_t i = 0; i + 1 < out.majors.size(); ++i) {
            float lo = out.majors[i], hi = out.majors[i + 1];
            float step = hi - lo;
            if (step <= 0.0f) continue;
            for (int k = 1; k < 5; ++k)
                out.minors.push_back(lo + step * float(k) / 5.0f);
        }
    }
    return out;
}

} // namespace volcano::render
