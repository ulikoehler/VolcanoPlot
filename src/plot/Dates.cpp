// volcano/plot/Dates.cpp — date locators, formatters, converters
#include "volcano/plot/Dates.hpp"
#include "volcano/plot/Axes.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <numeric>
#include <set>

namespace volcano::plot::dates {

namespace {
constexpr float kDaySecs = 86400.0f;

/// mpl RRuleLocator._create_rrule: the rrule `dtstart` is
/// `vmin - relativedelta(vmax, vmin)`.  Returns that anchor as a
/// day number (fractional).  relativedelta field normalization
/// follows dateutil._fix (borrow from the month preceding vmax).
inline double rruleDtstart(double vmin, double vmax) {
    const CivilTime a = civilFromNumD(vmin), b = civilFromNumD(vmax);
    int dY = b.year - a.year, dMo = int(b.month) - int(a.month),
        dD = int(b.day) - int(a.day), dH = b.hour - a.hour,
        dMi = b.minute - a.minute;
    double dS = b.second - a.second;
    if (dS < 0) { dS += 60; --dMi; }
    if (dMi < 0) { dMi += 60; --dH; }
    if (dH < 0) { dH += 24; --dD; }
    if (dD < 0) {
        const std::chrono::year_month_day prev{
            std::chrono::sys_days{std::chrono::year_month_day{
                std::chrono::year{b.year},
                std::chrono::month{b.month},
                std::chrono::day{1}}} - std::chrono::days{1}};
        dD += int(unsigned(prev.day()));
        --dMo;
    }
    if (dMo < 0) { dMo += 12; --dY; }
    // start = vmin - delta: year/month fields first (day clamped
    // like dateutil), then day/time fields.
    int y = a.year - dY;
    int mo = int(a.month) - dMo;
    while (mo < 1) { mo += 12; --y; }
    while (mo > 12) { mo -= 12; ++y; }
    if (y < 1) { y = 1; mo = 1; }
    const auto lastDay = std::chrono::year_month_day_last{
        std::chrono::year{y} / std::chrono::month{unsigned(mo)} /
        std::chrono::last};
    const int day = std::min(int(a.day), int(unsigned(lastDay.day())));
    const std::chrono::sys_days sd{std::chrono::year_month_day{
        std::chrono::year{y}, std::chrono::month{unsigned(mo)},
        std::chrono::day{unsigned(day)}}};
    // start = (a's date − y/mo delta, day clamped) + a's time-of-day
    // − the day/time delta fields.
    return double(sd.time_since_epoch().count()) - dD +
           (a.hour - dH) / 24.0 + (a.minute - dMi) / 1440.0 +
           (a.second - dS) / 86400.0;
}
} // namespace

CivilTime civilFromNumD(double days) {
    // Floor-based day+frac decomposition — no second rounding (a
    // symmetric ±0.5s bias corrupts relativedelta-style diffs).
    const double d0 = std::floor(days);
    const std::chrono::sys_days dd{std::chrono::days{int64_t(d0)}};
    std::chrono::year_month_day ymd{dd};
    const double todSecs = (days - d0) * 86400.0;
    // chrono weekday: 0=Sunday → mpl: 0=Monday.
    int wd = (int(std::chrono::weekday{dd}.c_encoding()) + 6) % 7;
    return {int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()),
            int(todSecs / 3600), int(std::fmod(todSecs, 3600) / 60),
            std::fmod(todSecs, 60.0), wd};
}

CivilTime civilFromNum(float days) { return civilFromNumD(days); }

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

std::vector<double> YearLocator::tickValuesD(double vmin,
                                               double vmax) const {
    auto lo = civilFromNumD(vmin), hi = civilFromNumD(vmax);
    // mpl YearLocator._create_rrule: rrule YEARLY interval=base from
    // dtstart = (base.le(vmin.year), month, day, 00:00) through
    // stop = (base.ge(vmax.year), same m/d) — ticks are NOT clipped
    // to [vmin, vmax]; a boundary year beyond vmax is included.
    int ymin = lo.year / step_ * step_;             // base.le, y ≥ 1
    if (ymin < 1) ymin = 1;
    int ymax = (hi.year + step_ - 1) / step_ * step_; // base.ge
    if (ymax > 9999) ymax = 9999;
    std::vector<double> out;
    for (int y = ymin; y <= ymax; y += step_) {
        std::chrono::year_month_day ymd{std::chrono::year{y},
            std::chrono::month{unsigned(month_)},
            std::chrono::day{unsigned(day_)}};
        if (!ymd.ok()) continue;
        out.push_back(double(std::chrono::sys_days{ymd}
                                 .time_since_epoch().count()));
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> MonthLocator::tickValuesD(double vmin,
                                               double vmax) const {
    auto lo = civilFromNumD(vmin), hi = civilFromNumD(vmax);
    if (!bymonth.empty()) {
        // mpl bymonth set: ticks on `day_` of the listed months.
        std::vector<double> out;
        for (int y = lo.year; y <= hi.year; ++y)
            for (int m : bymonth) {
                std::chrono::year_month_day ymd{
                    std::chrono::year{y},
                    std::chrono::month{unsigned(m)},
                    std::chrono::day{unsigned(day_)}};
                if (!ymd.ok()) continue;
                double t = std::chrono::sys_days{ymd}
                               .time_since_epoch().count();
                if (t >= vmin && t <= vmax) out.push_back(t);
            }
        std::ranges::sort(out);
        if (out.empty()) out = {vmin, vmax};
        return out;
    }
    // mpl rrule MONTHLY interval: months counted from
    // dtstart = vmin - relativedelta(vmax, vmin), ticks on day_.
    const auto sc = civilFromNumD(rruleDtstart(vmin, vmax));
    const int anchor = sc.year * 12 + int(sc.month) - 1;
    const int m0 = lo.year * 12 + int(lo.month) - 1;
    const int m1 = hi.year * 12 + int(hi.month) - 1;
    std::vector<double> out;
    for (int m = m0; m <= m1; ++m) {
        if ((m - anchor) % step_) continue;
        std::chrono::year_month_day ymd{std::chrono::year{m / 12},
            std::chrono::month{unsigned(m % 12 + 1)},
            std::chrono::day{unsigned(day_)}};
        if (!ymd.ok()) continue;
        double t = std::chrono::sys_days{ymd}
                       .time_since_epoch().count();
        if (t >= vmin && t <= vmax) out.push_back(t);
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> WeekdayLocator::tickValuesD(double vmin,
                                               double vmax) const {
    // mpl rrule DAILY interval + byweekday: every `step_`-th day from
    // dtstart, filtered to `weekday_`.
    const int64_t anchor =
        int64_t(std::floor(rruleDtstart(vmin, vmax)));
    const int64_t d0 = int64_t(std::floor(vmin)),
                  d1 = int64_t(std::ceil(vmax));
    std::vector<double> out;
    for (int64_t d = d0; d <= d1; ++d) {
        if ((d - anchor) % step_) continue;
        const auto c = civilFromNumD(double(d));
        if (int(c.weekday) != weekday_) continue;
        if (double(d) >= vmin && double(d) <= vmax)
            out.push_back((d));
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> DayLocator::tickValuesD(double vmin,
                                               double vmax) const {
    std::vector<double> out;
    const int64_t d0 = int64_t(std::floor(vmin)),
                  d1 = int64_t(std::ceil(vmax));
    if (!bymonthday.empty()) {
        // mpl bymonthday set: ticks on matching days-of-month.
        for (int64_t d = d0; d <= d1; ++d) {
            const auto c = civilFromNumD(double(d));
            if (std::ranges::find(bymonthday, int(c.day)) ==
                bymonthday.end())
                continue;
            if (double(d) >= vmin && double(d) <= vmax)
                out.push_back((d));
        }
        if (out.empty()) out = {vmin, vmax};
        return out;
    }
    // mpl rrule DAILY interval: every `step_`-th day from dtstart.
    const int64_t anchor =
        int64_t(std::floor(rruleDtstart(vmin, vmax)));
    for (int64_t d = d0; d <= d1; ++d)
        if ((d - anchor) % step_ == 0 && double(d) >= vmin &&
            double(d) <= vmax)
            out.push_back((d));
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> HourLocator::tickValuesD(double vmin,
                                               double vmax) const {
    std::vector<double> out;
    if (!byhour.empty()) {
        // mpl byhour set: ticks at the listed hours each day.
        const int64_t d0 = int64_t(std::floor(vmin)),
                      d1 = int64_t(std::ceil(vmax));
        for (int64_t d = d0; d <= d1; ++d)
            for (int h : byhour) {
                const double t = double(d) + h / 24.0;
                if (t >= vmin && t <= vmax) out.push_back((t));
            }
        if (out.empty()) out = {vmin, vmax};
        return out;
    }
    // mpl rrule HOURLY interval: every `step_`-th hour from dtstart
    // (absolute hour index), at minute/second 0.
    const int64_t anchor =
        int64_t(std::floor(rruleDtstart(vmin, vmax) * 24.0));
    const int64_t h0 = int64_t(std::floor(double(vmin) * 24.0)),
                  h1 = int64_t(std::ceil(double(vmax) * 24.0));
    for (int64_t h = h0; h <= h1; ++h) {
        if ((h - anchor) % step_) continue;
        const double t = double(h) / 24.0;
        if (t >= vmin && t <= vmax) out.push_back((t));
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> MinuteLocator::tickValuesD(double vmin,
                                               double vmax) const {
    std::vector<double> out;
    if (!byminute.empty()) {
        // mpl byminute set: ticks at the listed minutes each hour.
        const int64_t h0 = int64_t(std::floor(vmin * 24.0f)),
                      h1 = int64_t(std::ceil(vmax * 24.0f));
        for (int64_t h = h0; h <= h1; ++h)
            for (int m : byminute) {
                const double t = (double(h) + m / 60.0) / 24.0;
                if (t >= vmin && t <= vmax) out.push_back((t));
            }
        if (out.empty()) out = {vmin, vmax};
        return out;
    }
    // mpl rrule MINUTELY interval: every `step_`-th minute from
    // dtstart, at second 0.
    const int64_t anchor =
        int64_t(std::floor(rruleDtstart(vmin, vmax) * 1440.0));
    const int64_t m0 = int64_t(std::floor(double(vmin) * 1440.0)),
                  m1 = int64_t(std::ceil(double(vmax) * 1440.0));
    for (int64_t m = m0; m <= m1; ++m) {
        if ((m - anchor) % step_) continue;
        const double t = double(m) / 1440.0;
        if (t >= vmin && t <= vmax) out.push_back((t));
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> SecondLocator::tickValuesD(double vmin,
                                               double vmax) const {
    std::vector<double> out;
    if (!bysecond.empty()) {
        // mpl bysecond set: ticks at the listed seconds each minute.
        const int64_t m0 = int64_t(std::floor(vmin * 1440.0f)),
                      m1 = int64_t(std::ceil(vmax * 1440.0f));
        for (int64_t m = m0; m <= m1; ++m)
            for (int s : bysecond) {
                const double t = (double(m) + s / 60.0) / 1440.0;
                if (t >= vmin && t <= vmax) out.push_back((t));
            }
        if (out.empty()) out = {vmin, vmax};
        return out;
    }
    // mpl rrule SECONDLY interval: every `step_`-th second from
    // dtstart.
    const int64_t anchor =
        int64_t(std::floor(rruleDtstart(vmin, vmax) * 86400.0));
    const int64_t s0 = int64_t(std::floor(double(vmin) * 86400.0)),
                  s1 = int64_t(std::ceil(double(vmax) * 86400.0));
    for (int64_t s = s0; s <= s1; ++s) {
        if ((s - anchor) % step_) continue;
        const double t = double(s) / 86400.0;
        if (t >= vmin && t <= vmax) out.push_back((t));
    }
    if (out.empty()) out = {vmin, vmax};
    return out;
}

std::vector<double> MicrosecondLocator::tickValuesD(double vmin,
                                               double vmax) const {
    // mpl wraps a MultipleLocator over within-day microseconds —
    // ticks where (frac_day * µs_per_day) is a multiple of
    // `step_`, anchored at each midnight.
    std::vector<double> out;
    constexpr double kUsPerDay = 86400.0 * 1e6;
    const double day0 = std::floor(double(vmin));
    const double lo = (double(vmin) - day0) * kUsPerDay,
                 hi = (double(vmax) - day0) * kUsPerDay;
    // mpl MultipleLocator.tick_values on the µs grid:
    //   vmin = edge.ge(lo)*step;  n = (hi - vmin + 0.001*step)//step
    //   locs = vmin - step + arange(n+3)*step
    const double step = double(step_);
    const double d = std::floor(lo / step), m = lo - d * step;
    const double vminE = (std::abs(m / step) < 1e-10 ? d : d + 1) * step;
    const double n = std::floor((hi - vminE + 0.001 * step) / step);
    for (double i = 0; i < n + 3; ++i)
        out.push_back(day0 + (vminE - step + i * step) / kUsPerDay);
    return out;
}

// ─── AutoDateLocator ────────────────────────────────────────────────────────

std::vector<double> AutoDateLocator::tickValuesD(double vmin,
                                               double vmax) const {
    using namespace std::chrono;
    double span = vmax - vmin;
    if (!(span > 0.0f)) return {};

    // mpl get_locator: relativedelta-style decomposition of the range.
    const CivilTime a = civilFromNumD(vmin), b = civilFromNumD(vmax);
    int dY = b.year - a.year, dMo = int(b.month) - int(a.month),
        dD = int(b.day) - int(a.day), dH = b.hour - a.hour,
        dMi = b.minute - a.minute;
    int dS = int(b.second) - int(a.second);
    // Borrow-normalize negative fields (dateutil relativedelta._fix).
    if (dS < 0) { dS += 60; --dMi; }
    if (dMi < 0) { dMi += 60; --dH; }
    if (dH < 0) { dH += 24; --dD; }
    if (dD < 0) {
        const year_month_day prev =
            sys_days{year_month_day{year{b.year}, month{b.month}, day{1}}}
            - days{1};
        dD += int(unsigned(prev.day()));
        --dMo;
    }
    if (dMo < 0) { dMo += 12; --dY; }

    const double numDays = std::floor(double(span));      // tdelta.days
    const double nums[] = {
        double(dY),                                        // years
        dY * 12.0 + dMo,                                   // months
        numDays,                                           // days
        numDays * 24.0 + dH,                               // hours
        (numDays * 24.0 + dH) * 60.0 + dMi,                // minutes
        std::floor(double(span) * kDaySecs),               // seconds
        std::floor(double(span) * kDaySecs * 1e6),         // µs
    };
    const int freqs[] = {Yearly, Monthly, Daily, Hourly,
                         Minutely, Secondly, Microsecondly};
    // mpl `intervald` per frequency (index matches `freqs`).
    static const int ivDailyM[] = {1, 2, 4, 7, 14};
    static const int ivDaily[]  = {1, 2, 3, 7, 14, 21};
    const std::vector<int> intervald[7] = {
        {1, 2, 4, 5, 10, 20, 40, 50, 100, 200, 400, 500,
         1000, 2000, 4000, 5000, 10000},
        {1, 2, 3, 4, 6},
        intervalMultiples ? std::vector<int>(std::begin(ivDailyM),
                                             std::end(ivDailyM))
                          : std::vector<int>(std::begin(ivDaily),
                                             std::end(ivDaily)),
        {1, 2, 3, 4, 6, 12},
        {1, 5, 10, 15, 30},
        {1, 5, 10, 15, 30},
        {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
         10000, 20000, 50000, 100000, 200000, 500000, 1000000},
    };

    // First frequency with >= minticks range units wins; pick the first
    // interval bounded by maxticks[freq] (mpl get_locator loop).
    int freq = Yearly, interval = 1;
    for (int i = 0; i < 7; ++i) {
        if (nums[i] < double(minticks)) continue;
        freq = freqs[i];
        const int mt = maxticks.count(freq) ? maxticks.at(freq) : 8;
        for (int iv : intervald[i]) {
            interval = iv;
            if (nums[i] <= double(iv) * (mt - 1)) break;
        }
        break;
    }

    auto dayNum = [](int y, unsigned m, unsigned d) {
        // GCC 13 can't parse qualified-id{...} call args in lambdas.
        const std::chrono::sys_days sd{
            std::chrono::year_month_day{std::chrono::year{y},
                                        std::chrono::month{m},
                                        std::chrono::day{d}}};
        return dateToNum(sd);
    };

    switch (freq) {
    case Yearly:
        return YearLocator{interval}.tickValuesD(vmin, vmax);
    case Monthly: {
        if (!intervalMultiples)
            return MonthLocator{interval}.tickValuesD(vmin, vmax);
        // mpl bymonth = range(1,13)[::interval] → months anchored at
        // January, ticks on the 1st (bymonthday=1).
        std::vector<double> out;
        for (int y = a.year; y <= b.year + 1; ++y)
            for (unsigned m = 1; m <= 12; ++m) {
                if ((int(m) - 1) % interval) continue;
                double t = dayNum(y, m, 1);
                if (t >= vmin && t <= vmax) out.push_back(t);
            }
        return out;
    }
    case Daily: {
        if (!intervalMultiples)
            return DayLocator{interval}.tickValuesD(vmin, vmax);
        // mpl bymonthday = range(1,32)[::interval] with specials:
        // interval 7 → {1,8,15,22}, 14 → {1,15}.
        std::vector<int> doms;
        if (interval == 7) doms = {1, 8, 15, 22};
        else if (interval == 14) doms = {1, 15};
        else for (int d = 1; d <= 31; d += interval) doms.push_back(d);
        std::vector<double> out;
        const int64_t d0 = int64_t(std::floor(vmin)),
                      d1 = int64_t(std::ceil(vmax));
        for (int64_t d = d0; d <= d1; ++d) {
            const auto c = civilFromNumD(double(d));
            if (std::ranges::find(doms, int(c.day)) == doms.end())
                continue;
            if (double(d) >= vmin && double(d) <= vmax)
                out.push_back((d));
        }
        if (out.empty()) out = {vmin, vmax};
    return out;
    }
    case Hourly: {
        if (!intervalMultiples)
            return HourLocator{interval}.tickValuesD(vmin, vmax);
        // mpl byhour = range(0,24)[::interval] — hours anchored at
        // midnight, ticks at :00:00.
        HourLocator l{1};
        for (int h = 0; h < 24; h += interval) l.byhour.push_back(h);
        return l.tickValuesD(vmin, vmax);
    }
    case Minutely: {
        if (!intervalMultiples)
            return MinuteLocator{interval}.tickValuesD(vmin, vmax);
        MinuteLocator l{1};
        for (int m = 0; m < 60; m += interval) l.byminute.push_back(m);
        return l.tickValuesD(vmin, vmax);
    }
    case Secondly: {
        if (!intervalMultiples)
            return SecondLocator{interval}.tickValuesD(vmin, vmax);
        SecondLocator l{1};
        for (int s = 0; s < 60; s += interval) l.bysecond.push_back(s);
        return l.tickValuesD(vmin, vmax);
    }
    default:
        return MicrosecondLocator{interval}.tickValuesD(vmin, vmax);
    }
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

ConciseDateFormatter::ConciseDateFormatter(
    std::array<const char*, 6> formats,
    std::array<const char*, 6> offsetFormats,
    std::array<const char*, 6> zeroFormats,
    bool showOffset)
    : formats_(formats), offsetFormats_(offsetFormats),
      zeroFormats_(zeroFormats), showOffsetEnabled_(showOffset) {}

void ConciseDateFormatter::setLocs(std::span<const float> locs) {
    locs_.assign(locs.begin(), locs.end());
    showOffset_ = true;
    level_ = 5;
    if (locs.empty()) return;
    offsetBase_ = locs.back();  // mpl: last tick (first if inverted)
    // mpl format_ticks level detection: the coarsest tickdate field
    // that varies across ticks. Fields: year, month, day, hour, min,
    // sec → levels 0..5.
    auto fieldOf = [](const CivilTime& c, int lvl) -> double {
        switch (lvl) {
        case 0: return double(c.year);
        case 1: return double(c.month);
        case 2: return double(c.day);
        case 3: return double(c.hour);
        case 4: return double(c.minute);
        default: return c.second;
        }
    };
    std::vector<CivilTime> cs;
    cs.reserve(locs.size());
    for (float v : locs) cs.push_back(civilFromNumD(v));
    for (int lvl = 5; lvl >= 0; --lvl) {
        std::set<double> u;
        for (auto& c : cs) u.insert(fieldOf(c, lvl));
        if (u.size() > 1) {
            level_ = lvl;
            // If the discriminating values include the "zero" (1 for
            // month/day fields), the offset would be redundant.
            if (lvl < 2 && u.count(1)) showOffset_ = false;
            break;
        }
        if (lvl == 0) level_ = 5;
    }
    // mpl strips the common trailing zeros of '%S.%f' labels so all
    // sub-second labels share the same precision.
    stripZeros_ = 0;
    if (level_ >= 5) {
        int mn = -1;
        for (float v : locs) {
            auto s = secLabel(v);
            auto dot = s.find('.');
            if (dot == std::string::npos) continue;
            int tz = 0;
            for (int i = int(s.size()) - 1;
                 i > int(dot) && s[size_t(i)] == '0'; --i) ++tz;
            if (mn < 0 || tz < mn) mn = tz;
        }
        if (mn > 0) stripZeros_ = mn;
    }
}

std::string ConciseDateFormatter::secLabel(float v) const {
    auto c = civilFromNumD(v);
    // mpl '%S.%f' → "05.123456".
    const int isec = int(c.second);
    const int us = int(std::lround((c.second - isec) * 1e6));
    char buf[32];
    if (us > 0)
        std::snprintf(buf, sizeof(buf), "%02d.%06d", isec, us);
    else
        std::snprintf(buf, sizeof(buf), "%02d.000000", isec);
    std::string s = buf;
    if (stripZeros_ > 0) {
        // Trim up to stripZeros_ trailing zeros, then a trailing '.'.
        size_t n = std::min<size_t>(stripZeros_, s.size());
        s.resize(s.size() - n);
        while (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

std::string ConciseDateFormatter::format(float v, int) const {
    static const int kZerovals[6] = {0, 1, 1, 0, 0, 0};
    if (level_ < 0 || locs_.empty()) {
        // No setLocs yet: fall back to the median-gap heuristic.
        if (scaleDays_ >= 365.0f)       return strfnum(v, "%Y");
        if (scaleDays_ >= 28.0f)        return strfnum(v, "%b");
        if (scaleDays_ >= 1.0f)         return strfnum(v, "%d");
        if (scaleDays_ >= 1.0f / 24.0f) return strfnum(v, "%H:%M");
        return strfnum(v, "%H:%M:%S");
    }
    auto c = civilFromNumD(v);
    if (level_ >= 5) {
        // mpl: `second == microsecond == 0` → whole-minute boundary.
        if (c.second == 0.0) {
            // mpl zero_formats[5] = '%H:%M'.
            return strfnum(v, zeroFormats_[5]);
        }
        return secLabel(v);
    }
    double field = 0;
    switch (level_) {
    case 0: field = double(c.year);   break;
    case 1: field = double(c.month);  break;
    case 2: field = double(c.day);    break;
    case 3: field = double(c.hour);   break;
    case 4: field = double(c.minute); break;
    }
    const char* f = int(field) == kZerovals[level_]
                        ? zeroFormats_[level_] : formats_[level_];
    return strfnum(v, f);
}

std::string ConciseDateFormatter::offsetText() const {
    if (!showOffsetEnabled_ || !showOffset_ || locs_.empty()) return {};
    if (level_ < 0) return {};
    return strfnum(offsetBase_, offsetFormats_[level_]);
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

AxisInfo ConciseDateConverter::axisInfo(std::string_view,
                                        const Axes*) const {
    AxisInfo info;
    info.locator = std::make_shared<AutoDateLocator>();
    info.formatter = std::make_shared<ConciseDateFormatter>();
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
