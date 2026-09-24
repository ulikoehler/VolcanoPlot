// volcano/plot/Dates.hpp — matplotlib.dates equivalents: date→float
// conversion (days since epoch), date tick locators/formatters, and the
// DateConverter for the units registry.
#pragma once

#include "volcano/plot/Ticks.hpp"
#include "volcano/plot/Units.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <map>
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
        std::chrono::seconds(std::llround(days * 86400.0f))};
}

/// Double-precision variant — sub-day resolution at epoch-day
/// magnitudes needs float64 (float32 loses ~86 s at day 18000+).
[[nodiscard]] inline std::chrono::sys_seconds numToDateD(double days) {
    return std::chrono::sys_seconds{
        std::chrono::seconds(std::llround(days * 86400.0))};
}

/// Break a day-number into civil fields via chrono.
struct CivilTime {
    int year; unsigned month, day;   // month 1-12, day 1-31
    int hour, minute; double second;
    int weekday;                     // mpl convention: 0=Monday .. 6=Sunday
};
[[nodiscard]] CivilTime civilFromNum(float days);
[[nodiscard]] CivilTime civilFromNumD(double days);

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
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_, month_, day_;
};

/// mpl `MonthLocator`: ticks on `day` of every `step`-th month, or on
/// `day` of the months in `bymonth` when that set is non-empty.
class MonthLocator : public Locator {
public:
    explicit MonthLocator(int step = 1, int day = 1)
        : step_(step), day_(day) {}
    /// mpl `bymonth` set (1-12); empty → interval stepping.
    std::vector<int> bymonth;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
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
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_, weekday_;
};

/// mpl `DayLocator`: ticks every `step` days (epoch-aligned), or on the
/// days-of-month in `bymonthday` when that set is non-empty.
class DayLocator : public Locator {
public:
    explicit DayLocator(int step = 1) : step_(step) {}
    /// mpl `bymonthday` set; empty → interval stepping.
    std::vector<int> bymonthday;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_;
};

/// mpl `HourLocator`: ticks every `step` hours, or at the hours-of-day
/// in `byhour` when that set is non-empty.
class HourLocator : public Locator {
public:
    explicit HourLocator(int step = 1) : step_(step) {}
    /// mpl `byhour` set (0-23); empty → interval stepping.
    std::vector<int> byhour;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_;
};

/// mpl `MinuteLocator`: ticks every `step` minutes, or at the
/// minutes-of-hour in `byminute` when that set is non-empty.
class MinuteLocator : public Locator {
public:
    explicit MinuteLocator(int step = 1) : step_(step) {}
    /// mpl `byminute` set (0-59); empty → interval stepping.
    std::vector<int> byminute;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_;
};

/// mpl `SecondLocator`: ticks every `step` seconds, or at the
/// seconds-of-minute in `bysecond` when that set is non-empty.
class SecondLocator : public Locator {
public:
    explicit SecondLocator(int step = 1) : step_(step) {}
    /// mpl `bysecond` set (0-59); empty → interval stepping.
    std::vector<int> bysecond;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int step_;
};

/// mpl `MicrosecondLocator`: ticks every `step` microseconds.
class MicrosecondLocator : public Locator {
public:
    explicit MicrosecondLocator(int64_t step = 1) : step_(step) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
private:
    int64_t step_;
};

/// mpl `AutoDateLocator`: picks the finest scale (year → microsecond)
/// that yields at least `minticks` ticks, choosing an interval from
/// mpl's `intervald` tables so that at most `maxticks[freq]` ticks
/// appear. With `intervalMultiples` (mpl default) month/day/hour/minute
/// ticks are anchored on calendar boundaries (bymonth/bymonthday/
/// byhour/byminute sets); otherwise epoch-aligned intervals are used.
class AutoDateLocator : public Locator {
public:
    /// mpl frequency keys (same ints as matplotlib.dates constants).
    enum Freq : int {
        Yearly = 0, Monthly = 1, Weekly = 2, Daily = 3,
        Hourly = 4, Minutely = 5, Secondly = 6, Microsecondly = 7,
    };
    int minticks = 5;
    /// mpl `maxticks` dict keyed by the Freq ints.
    std::map<int, int> maxticks = {{Yearly, 11}, {Monthly, 12},
                                   {Daily, 11}, {Hourly, 12},
                                   {Minutely, 11}, {Secondly, 11},
                                   {Microsecondly, 8}};
    bool intervalMultiples = true;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override {
        const auto d = tickValuesD(vmin, vmax);
        return {d.begin(), d.end()};
    }
    [[nodiscard]] std::vector<double> tickValuesD(double vmin,
                                                 double vmax) const override;
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
/// with the larger context carried in `offsetText()`. Follows mpl's
/// format_ticks algorithm: the label level is the coarsest date field
/// that varies across ticks; ticks on a "zero" boundary use the
/// level's zero-format; the offset text shows context the labels omit.
class ConciseDateFormatter : public Formatter {
public:
    /// mpl signature: ConciseDateFormatter(locator=None, tz=None,
    /// formats=None, offset_formats=None, zero_formats=None,
    /// show_offset=True). Defaults are mpl's format tables.
    explicit ConciseDateFormatter(
        std::array<const char*, 6> formats =
            {"%Y", "%b", "%d", "%H:%M", "%H:%M", "%S.%f"},
        std::array<const char*, 6> offsetFormats =
            {"", "%Y", "%Y-%b", "%Y-%b-%d", "%Y-%b-%d %H:%M",
             "%Y-%b-%d %H:%M"},
        std::array<const char*, 6> zeroFormats =
            {"", "%Y", "%b", "%b-%d", "%H:%M", "%H:%M"},
        bool showOffset = true);
    void setLocs(std::span<const float> locs) override;
    [[nodiscard]] std::string format(float v, int pos) const override;
    [[nodiscard]] std::string offsetText() const override;
private:
    std::vector<float> locs_;
    std::array<const char*, 6> formats_;
    std::array<const char*, 6> offsetFormats_;
    std::array<const char*, 6> zeroFormats_;
    bool showOffsetEnabled_ = true;  // ctor arg (mpl show_offset)
    float scaleDays_ = 1.0f;
    float offsetBase_ = 0.0f;  // last loc, for the context label
    int level_ = -1;           // mpl tickdate level 0..5 (-1 = unset)
    bool showOffset_ = true;
    int stripZeros_ = 0;       // common '%S.%f' trailing zeros to trim
    [[nodiscard]] std::string secLabel(float v) const;
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

/// mpl `dates.ConciseDateConverter`: same conversion as DateConverter
/// but `axisInfo` supplies ConciseDateFormatter as the major formatter.
class ConciseDateConverter : public DateConverter {
public:
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
