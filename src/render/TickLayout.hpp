// src/render/TickLayout.hpp — tick/layout helpers shared between the
// raster Renderer and the VectorRenderer driver.
#pragma once

#include "volcano/plot/Scale.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Ticks.hpp"

#include <span>
#include <string>
#include <vector>

namespace volcano::plot { class Normalize; }

namespace volcano::render {

/// Simple "nice number" tick locator (matplotlib MaxNLocator style).
std::vector<float> autoTicks(float vmin, float vmax, int nbins);
/// Format a tick value as a short string, decimals derived from the step.
std::string formatTick(float v, float step);
/// Compute the nice step size used by autoTicks.
float autoTickStep(float vmin, float vmax, int nbins);

/// Major tick positions honoring TickConfig locator/positions overrides.
/// When `axisLengthPx` > 0 the effective nbins is capped by how many
/// labels fit along the axis (matplotlib AutoLocator's get_tick_space:
/// axis length in points / (label point size × factor), factor 3 for
/// x and 2 for y). `fontPt`/`dpi` convert pixels to points.
std::vector<float> axisTicks(const plot::TickConfig& tc,
                             const plot::AxisScale& scale,
                             float lo, float hi,
                             float axisLengthPx = 0.0f,
                             float fontPt = 10.0f,
                             float dpi = 100.0f,
                             bool yAxis = false);
/// Whether minor tick marks are drawn for this axis.
bool minorEnabled(const plot::TickConfig& tc, const plot::AxisScale& s);
/// Minor tick positions: explicit minorLocator, log-family subs, or
/// linear subdivisions between majors (AutoMinorLocator).
std::vector<float> axisMinorTicks(const plot::TickConfig& tc,
                                  const plot::AxisScale& scale,
                                  float lo, float hi,
                                  std::span<const float> majors);
/// Formatter for an axis: explicit formatter, legacy format string,
/// log-scale sci-notation, or default ScalarFormatter (setLocs gives
/// offset/scientific detection). The view interval [vmin, vmax] is
/// forwarded via Formatter::setViewInterval (mpl set_locs) so log
/// formatters can apply minor_thresholds label suppression.
plot::Formatter* axisFormatter(const plot::TickConfig& tc,
                               std::span<const float> ticks,
                               const plot::FigureStyle& style,
                               const plot::AxisScale& scale,
                               float vmin, float vmax,
                               plot::ScalarFormatter& defaultFmt,
                               plot::FormatStrFormatter& strFmt,
                               plot::LogFormatterMathtext& logFmt);
/// Whether this axis defaults to log-scale minor tick labels
/// (matplotlib log axes label minor subs via LogFormatterSciNotation).
bool logMinorLabels(const plot::TickConfig& tc,
                    const plot::AxisScale& scale);
/// Label for tick `t` at index `i`, honoring fixed labels first.
std::string tickLabel(const plot::TickConfig& tc, const plot::Formatter& f,
                      float t, int i);
/// mpl Formatter.__call__ wraps every label in fix_minus: ASCII '-'
/// becomes U+2212 '−' when axes.unicode_minus is on (mpl default).
std::string fixMinus(std::string s);
/// mpl Formatter.__call__ per-formatter semantics: fix_minus applies
/// only to formatters whose __call__ wraps it (Formatter::unicodeMinus).
inline std::string fmtLabel(const plot::Formatter& f, float v, int pos) {
    auto s = f.format(v, pos);
    return f.unicodeMinus() ? fixMinus(std::move(s)) : s;
}
/// TickConfig direction string → fraction of tick length inside the axes.
float tickInFrac(const plot::TickConfig& tc);

/// Colorbar tick set: major positions + formatted labels + unlabeled
/// minor marks. Honors mpl colorbar behavior: explicit ticks/labels win;
/// a LogNorm mappable gets `_ColorbarLogLocator` decades labeled via
/// LogFormatterSciNotation ("$10^{k}$" mathtext) plus subs minor marks;
/// SymLogNorm gets SymmetricalLogLocator; linear norms get autoTicks +
/// formatTick and `minorTicksOn` subdivisions (AutoMinorLocator /5).
struct ColorbarTickSet {
    std::vector<float> majors;
    std::vector<std::string> labels;
    std::vector<float> minors;
};
ColorbarTickSet colorbarTicks(std::span<const float> explicitTicks,
                              std::span<const std::string> explicitLabels,
                              bool minorTicksOn,
                              const plot::Normalize* norm,
                              float vmin, float vmax);

} // namespace volcano::render
