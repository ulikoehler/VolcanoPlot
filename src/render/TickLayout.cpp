// src/render/TickLayout.cpp — tick/layout helpers shared between the
// raster Renderer and the VectorRenderer driver.
#include "TickLayout.hpp"

#include <cmath>
#include <format>

namespace volcano::render {

/// Simple "nice number" tick locator (matplotlib MaxNLocator style).
/// Returns ~nbins tick positions within [vmin, vmax].
/// matplotlib's MaxNLocator with default steps=[1,2,5,10] picks the
/// nice step that gives at most nbins ticks.
std::vector<float> autoTicks(float vmin, float vmax, int nbins) {
    if (vmin >= vmax) return {};
    float range = vmax - vmin;
    // matplotlib's MaxNLocator tries steps [1, 2, 2.5, 5, 10] × 10^k and picks
    // the smallest step that gives at most nbins+1 ticks (i.e. the most
    // ticks without exceeding nbins).
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    // Try nice steps from smallest to largest, pick the FIRST one that
    // gives <= nbins+1 ticks. This maximizes the number of ticks.
    float niceSteps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
    float niceStep = 10.0f * mag;  // fallback: largest step
    for (float s : niceSteps) {
        float step = s * mag;
        int numTicks = int(std::floor(vmax / step) - std::ceil(vmin / step)) + 1;
        if (numTicks <= nbins + 1) {
            niceStep = step;
            break;  // first (smallest) step that fits
        }
    }

    float start = std::ceil(vmin / niceStep) * niceStep;
    std::vector<float> ticks;
    for (float v = start; v <= vmax + niceStep * 0.001f; v += niceStep) {
        // Round to avoid floating-point drift accumulating.
        float k = std::round(v / niceStep);
        ticks.push_back(k * niceStep);
    }
    return ticks;
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
    if (vmin >= vmax) return 1.0f;
    float range = vmax - vmin;
    float rawStep = range / nbins;
    float mag = std::pow(10.0f, std::floor(std::log10(rawStep)));
    float niceSteps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
    float niceStep = 10.0f * mag;
    for (float s : niceSteps) {
        float step = s * mag;
        int numTicks = int(std::floor(vmax / step) - std::ceil(vmin / step)) + 1;
        if (numTicks <= nbins + 1) {
            niceStep = step;
            break;
        }
    }
    return niceStep;
}

// ─── Locator/formatter plumbing (matplotlib ticker framework) ───────────────

/// Major tick positions honoring TickConfig locator/positions overrides.
std::vector<float> axisTicks(const plot::TickConfig& tc,
                             const plot::AxisScale& scale,
                             float lo, float hi,
                             float axisLengthPx,
                             float fontPt, float dpi, bool yAxis) {
    if (tc.locator) return tc.locator->tickValues(lo, hi);
    // An explicitly-set positions list is honored even when empty
    // (matplotlib ax.set_xticks([]) hides the ticks entirely).
    if (tc.positions) return *tc.positions;
    int nb = tc.nbins;
    if (axisLengthPx > 0.0f) {
        // mpl Axis.get_tick_space: estimated labels that fit along the
        // axis. X assumes ≤3:1 text aspect, Y uses 2× line spacing.
        float lengthPt = axisLengthPx * 72.0f / std::max(dpi, 1.0f);
        float labelPt = std::max(fontPt, 1.0f) * (yAxis ? 2.0f : 3.0f);
        int space = int(std::floor(lengthPt / labelPt));
        nb = std::min(nb, std::max(space, 2));
    }
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
    if (scale.kind == plot::ScaleKind::Log ||
        scale.kind == plot::ScaleKind::FunctionLog)
        return plot::LogLocator{}.minorValues(lo, hi);
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
    // mpl log axes default to LogFormatterSciNotation ($m×10^{k}$).
    if (!f && (scale.kind == plot::ScaleKind::Log ||
               scale.kind == plot::ScaleKind::FunctionLog))
        f = &logFmt;
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
                return (*tc.labels)[k];
    }
    return f.format(t, i);
}

/// TickConfig direction string → fraction of tick length inside the axes.
float tickInFrac(const plot::TickConfig& tc) {
    if (tc.direction == "in") return 1.0f;
    if (tc.direction == "inout") return 0.5f;
    return 0.0f; // "out"
}

} // namespace volcano::render
