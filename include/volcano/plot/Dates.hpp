// volcano/plot/Dates.hpp — matplotlib.dates equivalents: date→float
// conversion (days since epoch), date tick locators/formatters, and the
// DateConverter for the units registry.
#pragma once

#include "volcano/plot/Ticks.hpp"
#include "volcano/plot/Units.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace volcano::plot::dates {

/// mpl `date2num`: days since 1970-01-01 UTC (proleptic Gregorian).
/// Accepts any chrono time_point/duration — sys_days, sys_seconds, etc.
template <class Rep, class Period>
[[nodiscard]] constexpr float
durationToNum(std::chrono::duration<Rep, Period> d) {
    return std::chrono::duration<float, std::chrono::days::period>(d)
        .count();
}
template <class Clock, class Dur>
[[nodiscard]] constexpr float
dateToNum(std::chrono::time_point<Clock, Dur> t) {
    return durationToNum(t.time_since_epoch());
}

/// mpl `num2date`: days-since-epoch float → sys_seconds (UTC).
[[nodiscard]] inline std::chrono::sys_seconds numToDate(float days) {
    return std::chrono::sys_seconds{
        std::chrono::seconds(static_cast<int64_t>(days * 86400.0f + 0.5f))};
}

/// Break a day-number into civil fields via chrono.
struct CivilTime {
    int year; unsigned month, day;   // month 1-12, day 1-31
    int hour, minute; double second;
    int weekday;                     // mpl convention: 0=Monday .. 6=Sunday
};
[[nodiscard]] CivilTime civilFromNum(float days);

/// Format via strftime on the civil time (C locale month/day names).
[[nodiscard]] std::string strfnum(float days, const char* fmt);

// ─── Locators ───────────────────────────────────────────────────────────────

/// mpl `YearLocator`: ticks at (month, day) of every `step`-th year
/// (base-aligned like mpl's `base` argument).
class YearLocator : public Locator {
public:
    explicit YearLocator(int step = 1, int month = 1, int day = 1)
        : step_(step), month_(month), day_(day) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_, month_, day_;
};

/// mpl `MonthLocator`: ticks on `day` of every `step`-th month.
class MonthLocator : public Locator {
public:
    explicit MonthLocator(int step = 1, int day = 1)
        : step_(step), day_(day) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_, day_;
};

/// mpl `WeekdayLocator`: ticks on `weekday` (0=Mon) of every `step`-th
/// week.
class WeekdayLocator : public Locator {
public:
    explicit WeekdayLocator(int step = 1, int weekday = 0)
        : step_(step), weekday_(weekday) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_, weekday_;
};

/// mpl `DayLocator`: ticks every `step` days (epoch-aligned).
class DayLocator : public Locator {
public:
    explicit DayLocator(int step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_;
};

/// mpl `HourLocator`: ticks every `step` hours.
class HourLocator : public Locator {
public:
    explicit HourLocator(int step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_;
};

/// mpl `MinuteLocator`: ticks every `step` minutes.
class MinuteLocator : public Locator {
public:
    explicit MinuteLocator(int step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_;
};

/// mpl `SecondLocator`: ticks every `step` seconds.
class SecondLocator : public Locator {
public:
    explicit SecondLocator(int step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int step_;
};

/// mpl `MicrosecondLocator`: ticks every `step` microseconds.
class MicrosecondLocator : public Locator {
public:
    explicit MicrosecondLocator(int64_t step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int64_t step_;
};

/// mpl `AutoDateLocator`: picks the finest scale (year → microsecond)
/// that yields between `minticks` and `maxticks` ticks for the range.
class AutoDateLocator : public Locator {
public:
    int minticks = 3;
    int maxticks = 8;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
};

// ─── Formatters ─────────────────────────────────────────────────────────────

/// mpl `DateFormatter`: strftime format string applied to each tick.
class DateFormatter : public Formatter {
public:
    explicit DateFormatter(std::string fmt) : fmt_(std::move(fmt)) {}
    [[nodiscard]] std::string format(float v, int) const override {
        return strfnum(v, fmt_.c_str());
    }
private:
    std::string fmt_;
};

/// mpl `AutoDateFormatter`: format depends on the active locator scale
/// (learned via setLocs) and shows the date on day-boundary ticks.
class AutoDateFormatter : public Formatter {
public:
    void setLocs(std::span<const float> locs) override;
    [[nodiscard]] std::string format(float v, int pos) const override;
private:
    float scaleDays_ = 1.0f;   // median spacing of the tick set
};

/// mpl `ConciseDateFormatter`: minimal labels ("05", "Jan", "15:30")
/// with the larger context carried in `offsetText()`.
class ConciseDateFormatter : public Formatter {
public:
    void setLocs(std::span<const float> locs) override;
    [[nodiscard]] std::string format(float v, int pos) const override;
    [[nodiscard]] std::string offsetText() const override;
private:
    float scaleDays_ = 1.0f;
    float offsetBase_ = 0.0f;  // first loc, for the context label
};

// ─── Units converter ────────────────────────────────────────────────────────

/// mpl `dates.DateConverter`: converts chrono time_points (sys_days,
/// sys_seconds, ...) to day numbers; axisInfo supplies date ticks.
class DateConverter : public UnitConverter {
public:
    [[nodiscard]] float convert(const std::any& value, Axes* ax,
                                char axis) const override;
    [[nodiscard]] AxisInfo axisInfo(std::string_view which,
                                    const Axes* ax) const override;
};

/// mpl `categories.StrCategoryConverter`: maps strings to consecutive
/// indices (order of first appearance, recorded per axis).
class StrCategoryConverter : public UnitConverter {
public:
    [[nodiscard]] float convert(const std::any& value, Axes* ax,
                                char axis) const override;
    [[nodiscard]] AxisInfo axisInfo(std::string_view which,
                                    const Axes* ax) const override;
};

} // namespace volcano::plot::dates
