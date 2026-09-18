// volcano/plot/Dates.cpp — date locators, formatters, converters
#include "volcano/plot/Dates.hpp"
#include "volcano/plot/Axes.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <numeric>

namespace volcano::plot::dates {

namespace {
constexpr float kDaySecs = 86400.0f;
} // namespace

CivilTime civilFromNum(float days) {
    auto ss = numToDate(days);
    auto dd = std::chrono::floor<std::chrono::days>(ss);
    std::chrono::year_month_day ymd{dd};
    double todSecs =
        std::chrono::duration<double>(ss - dd).count();
    // chrono weekday: 0=Sunday → mpl: 0=Monday.
    int wd = (int(std::chrono::weekday{dd}.c_encoding()) + 6) % 7;
    return {int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()),
            int(todSecs / 3600), int(std::fmod(todSecs, 3600) / 60),
            std::fmod(todSecs, 60.0), wd};
}

std::string strfnum(float days, const char* fmt) {
    auto c = civilFromNum(days);
    std::tm tm{};
    tm.tm_year = c.year - 1900;
    tm.tm_mon = int(c.month) - 1;
    tm.tm_mday = int(c.day);
    tm.tm_hour = c.hour;
    tm.tm_min = c.minute;
    tm.tm_sec = int(c.second);
    char buf[128];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

// ─── Locators ───────────────────────────────────────────────────────────────

std::vector<float> YearLocator::tickValues(float vmin, float vmax) const {
    auto lo = civilFromNum(vmin), hi = civilFromNum(vmax);
    int y0 = lo.year - (lo.year % step_);
    std::vector<float> out;
    for (int y = y0; y <= hi.year + step_; y += step_) {
        std::chrono::year_month_day ymd{std::chrono::year{y},
            std::chrono::month{unsigned(month_)},
            std::chrono::day{unsigned(day_)}};
        if (!ymd.ok()) continue;
        float t = dateToNum(std::chrono::sys_days{ymd});
        if (t >= vmin && t <= vmax) out.push_back(t);
    }
    return out;
}

std::vector<float> MonthLocator::tickValues(float vmin, float vmax) const {
    auto lo = civilFromNum(vmin), hi = civilFromNum(vmax);
    int m0 = lo.year * 12 + int(lo.month) - 1;
    int m1 = hi.year * 12 + int(hi.month) - 1;
    m0 -= m0 % step_;
    std::vector<float> out;
    for (int m = m0; m <= m1 + step_; m += step_) {
        std::chrono::year_month_day ymd{std::chrono::year{m / 12},
            std::chrono::month{unsigned(m % 12 + 1)},
            std::chrono::day{unsigned(day_)}};
        if (!ymd.ok()) continue;
        float t = dateToNum(std::chrono::sys_days{ymd});
        if (t >= vmin && t <= vmax) out.push_back(t);
    }
    return out;
}

std::vector<float> WeekdayLocator::tickValues(float vmin, float vmax) const {
    auto lo = civilFromNum(vmin);
    // First occurrence of `weekday_` on/after floor(vmin), then every
    // `step_` weeks.
    int delta = (weekday_ - lo.weekday + 7) % 7;
    float t0 = std::floor(vmin) + float(delta);
    std::vector<float> out;
    for (float t = t0; t <= vmax + 1e-6f; t += 7.0f * step_)
        if (t >= vmin) out.push_back(t);
    return out;
}

std::vector<float> DayLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float t0 = std::ceil(vmin / step_) * float(step_);
    for (float t = t0; t <= vmax + 1e-6f; t += float(step_))
        out.push_back(t);
    return out;
}

std::vector<float> HourLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float stepDays = float(step_) / 24.0f;
    float t0 = std::ceil(vmin / stepDays - 1e-9f) * stepDays;
    for (float t = t0; t <= vmax + 1e-9f; t += stepDays) out.push_back(t);
    return out;
}

std::vector<float> MinuteLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float stepDays = float(step_) / 1440.0f;
    float t0 = std::ceil(vmin / stepDays - 1e-9f) * stepDays;
    for (float t = t0; t <= vmax + 1e-9f; t += stepDays) out.push_back(t);
    return out;
}

std::vector<float> SecondLocator::tickValues(float vmin, float vmax) const {
    std::vector<float> out;
    float stepDays = float(step_) / kDaySecs;
    float t0 = std::ceil(vmin / stepDays - 1e-9f) * stepDays;
    for (float t = t0; t <= vmax + 1e-9f; t += stepDays) out.push_back(t);
    return out;
}

std::vector<float> MicrosecondLocator::tickValues(float vmin,
                                                  float vmax) const {
    std::vector<float> out;
    double stepDays = double(step_) / (kDaySecs * 1e6);
    double t0 = std::ceil(vmin / stepDays - 1e-9) * stepDays;
    for (double t = t0; t <= vmax + 1e-12; t += stepDays)
        out.push_back(float(t));
    return out;
}

// ─── AutoDateLocator ────────────────────────────────────────────────────────

namespace {

/// Smallest step ≥ raw from the allowed set.
int snapStep(float raw, std::initializer_list<int> allowed) {
    int last = 1;
    for (int s : allowed) {
        if (float(s) >= raw) return s;
        last = s;
    }
    return last;
}

} // namespace

std::vector<float> AutoDateLocator::tickValues(float vmin,
                                               float vmax) const {
    float span = vmax - vmin;
    if (!(span > 0.0f)) return {};

    std::vector<float> ticks;
    auto fit = [&](const Locator& loc) {
        auto t = loc.tickValues(vmin, vmax);
        if (int(t.size()) >= minticks) ticks = std::move(t);
        return int(t.size()) >= minticks;
    };

    if (span > 365.0f * 3.0f) {
        // Years: snap to 1/2/5×10^k.
        float raw = span / 365.0f / float(maxticks);
        int mag = int(std::floor(std::log10(std::max(raw, 1.0f))));
        int step = 1;
        for (int base = mag; base <= mag + 1 && step == 1; ++base) {
            int scale = 1;
            for (int i = 0; i < base; ++i) scale *= 10;
            for (int m : {1, 2, 5, 10})
                if (float(m * scale) >= raw) { step = m * scale; break; }
        }
        if (step == 1 && raw > 1.0f)
            step = int(std::ceil(raw));
        fit(YearLocator{step});
    } else if (span > 60.0f) {
        int months = snapStep(span / 30.0f / float(maxticks),
                              {1, 2, 3, 4, 6});
        fit(MonthLocator{months});
    } else if (span > 4.0f) {
        int days = snapStep(span / float(maxticks), {1, 2, 3, 7, 14});
        fit(DayLocator{days});
    } else if (span > 0.15f) {
        int hours = snapStep(span * 24.0f / float(maxticks),
                             {1, 3, 6, 12});
        fit(HourLocator{hours});
    } else if (span > 0.004f) {
        int mins = snapStep(span * 1440.0f / float(maxticks),
                            {1, 5, 15, 30});
        fit(MinuteLocator{mins});
    } else if (span > 5.0e-5f) {
        int secs = snapStep(span * kDaySecs / float(maxticks),
                            {1, 5, 15, 30});
        fit(SecondLocator{secs});
    } else {
        int64_t us = int64_t(span * kDaySecs * 1e6f / float(maxticks));
        int64_t step = std::max<int64_t>(1, us);
        fit(MicrosecondLocator{step});
    }
    return ticks;
}

// ─── Formatters ─────────────────────────────────────────────────────────────

void AutoDateFormatter::setLocs(std::span<const float> locs) {
    if (locs.size() < 2) { scaleDays_ = 1.0f; return; }
    std::vector<float> gaps;
    for (size_t i = 1; i < locs.size(); ++i)
        gaps.push_back(locs[i] - locs[i - 1]);
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2,
                     gaps.end());
    scaleDays_ = gaps[gaps.size() / 2];
}

std::string AutoDateFormatter::format(float v, int) const {
    float dayFrac = v - std::floor(v);
    // mpl: day-boundary ticks in sub-day scales show the full date.
    if (scaleDays_ < 1.0f && std::abs(dayFrac) < 1e-6f)
        return strfnum(v, "%Y-%m-%d");
    if (scaleDays_ >= 365.0f) return strfnum(v, "%Y");
    if (scaleDays_ >= 28.0f)  return strfnum(v, "%Y-%m");
    if (scaleDays_ >= 1.0f)   return strfnum(v, "%Y-%m-%d");
    if (scaleDays_ >= 1.0f / 1440.0f) return strfnum(v, "%H:%M");
    return strfnum(v, "%H:%M:%S");
}

void ConciseDateFormatter::setLocs(std::span<const float> locs) {
    if (locs.empty()) return;
    offsetBase_ = locs.front();
    if (locs.size() < 2) return;
    std::vector<float> gaps;
    for (size_t i = 1; i < locs.size(); ++i)
        gaps.push_back(locs[i] - locs[i - 1]);
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2,
                     gaps.end());
    scaleDays_ = gaps[gaps.size() / 2];
}

std::string ConciseDateFormatter::format(float v, int) const {
    if (scaleDays_ >= 365.0f)       return strfnum(v, "%Y");
    if (scaleDays_ >= 28.0f)        return strfnum(v, "%b");
    if (scaleDays_ >= 1.0f)         return strfnum(v, "%d");
    if (scaleDays_ >= 1.0f / 24.0f) return strfnum(v, "%H:%M");
    return strfnum(v, "%H:%M:%S");
}

std::string ConciseDateFormatter::offsetText() const {
    if (scaleDays_ >= 365.0f) return {};
    if (scaleDays_ >= 1.0f)   return strfnum(offsetBase_, "%b %Y");
    return strfnum(offsetBase_, "%Y-%m-%d");
}

// ─── Converters ─────────────────────────────────────────────────────────────

float DateConverter::convert(const std::any& value, Axes*, char) const {
    using namespace std::chrono;
    if (auto* p = std::any_cast<sys_days>(&value))
        return dateToNum(*p);
    if (auto* p = std::any_cast<sys_seconds>(&value))
        return durationToNum(p->time_since_epoch());
    if (auto* p = std::any_cast<sys_time<milliseconds>>(&value))
        return durationToNum(p->time_since_epoch());
    if (auto* p = std::any_cast<sys_time<microseconds>>(&value))
        return durationToNum(p->time_since_epoch());
    if (auto* p = std::any_cast<system_clock::time_point>(&value))
        return durationToNum(p->time_since_epoch());
    return 0.0f;
}

AxisInfo DateConverter::axisInfo(std::string_view, const Axes*) const {
    AxisInfo info;
    info.locator = std::make_shared<AutoDateLocator>();
    info.formatter = std::make_shared<AutoDateFormatter>();
    return info;
}

float StrCategoryConverter::convert(const std::any& value,
                                    Axes* ax, char axis) const {
    if (!ax) return 0.0f;
    const auto& s = std::any_cast<const std::string&>(value);
    return float(axis == 'y' ? ax->yCategoryIndex(s)
                            : ax->xCategoryIndex(s));
}

AxisInfo StrCategoryConverter::axisInfo(std::string_view which,
                                        const Axes* ax) const {
    AxisInfo info;
    if (!ax) return info;
    const auto& cats = which == "y" ? ax->yCategories()
                                    : ax->xCategories();
    std::vector<float> locs(cats.size());
    std::iota(locs.begin(), locs.end(), 0.0f);
    info.locator = std::make_shared<FixedLocator>(std::move(locs));
    info.formatter = std::make_shared<FixedFormatter>(cats);
    return info;
}

} // namespace volcano::plot::dates
