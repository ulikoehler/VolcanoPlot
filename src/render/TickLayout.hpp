// src/render/TickLayout.hpp — tick/layout helpers shared between the
// raster Renderer and the VectorRenderer driver.
#pragma once

#include "volcano/plot/Scale.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Ticks.hpp"

#include <span>
#include <string>
#include <vector>

namespace volcano::render {

/// Tick size/width are configured in points; render at ~96dpi (pt → px).
constexpr float kPtToPx = 96.0f / 72.0f;

/// Simple "nice number" tick locator (matplotlib MaxNLocator style).
std::vector<float> autoTicks(float vmin, float vmax, int nbins);
/// Format a tick value as a short string, decimals derived from the step.
std::string formatTick(float v, float step);
/// Compute the nice step size used by autoTicks.
float autoTickStep(float vmin, float vmax, int nbins);

/// Major tick positions honoring TickConfig locator/positions overrides.
std::vector<float> axisTicks(const plot::TickConfig& tc,
                             const plot::AxisScale& scale,
                             float lo, float hi);
/// Whether minor tick marks are drawn for this axis.
bool minorEnabled(const plot::TickConfig& tc, const plot::AxisScale& s);
/// Minor tick positions: explicit minorLocator, log-family subs, or
/// linear subdivisions between majors (AutoMinorLocator).
std::vector<float> axisMinorTicks(const plot::TickConfig& tc,
                                  const plot::AxisScale& scale,
                                  float lo, float hi,
                                  std::span<const float> majors);
/// Formatter for an axis: explicit formatter, legacy format string, or
/// default ScalarFormatter (setLocs gives offset/scientific detection).
plot::Formatter* axisFormatter(const plot::TickConfig& tc,
                               std::span<const float> ticks,
                               const plot::FigureStyle& style,
                               plot::ScalarFormatter& defaultFmt,
                               plot::FormatStrFormatter& strFmt);
/// Label for tick `t` at index `i`, honoring fixed labels first.
std::string tickLabel(const plot::TickConfig& tc, const plot::Formatter& f,
                      float t, int i);
/// TickConfig direction string → fraction of tick length inside the axes.
float tickInFrac(const plot::TickConfig& tc);

} // namespace volcano::render
