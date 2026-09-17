// tests/test_units.cpp — units registry, date locators/formatters,
// categorical axes
#include "PlotTestHarness.hpp"

#include <volcano/plot/Plot.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Units.hpp>
#include <volcano/plot/Dates.hpp>
#include <volcano/plot/plots/LinePlot.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;
using namespace std::chrono;

namespace {

sys_days ymd(int y, int m, int d) {
    return sys_days{year{y} / month{unsigned(m)} / day{unsigned(d)}};
}
float num(int y, int m, int d) { return dates::dateToNum(ymd(y, m, d)); }

struct CatFigure {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;

    explicit CatFigure(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
        axes->rect = {0, 0, size, size};
    }
    Image render() { return harness.render(figure); }
};

} // namespace

// ═══ Conversion ═══════════════════════════════════════════════════════════

TEST(DateUnits, EpochIsZero) {
    EXPECT_FLOAT_EQ(dates::dateToNum(sys_days{}), 0.0f);
    EXPECT_FLOAT_EQ(dates::dateToNum(sys_seconds{}), 0.0f);
}

TEST(DateUnits, RoundTrip) {
    auto d = ymd(2024, 6, 15);
    float n = dates::dateToNum(d);
    EXPECT_EQ(std::chrono::floor<days>(dates::numToDate(n)), d);
    auto c = dates::civilFromNum(n);
    EXPECT_EQ(c.year, 2024);
    EXPECT_EQ(c.month, 6u);
    EXPECT_EQ(c.day, 15u);
    EXPECT_EQ(c.hour, 0);
}

TEST(DateUnits, CivilWeekdayIsMplConvention) {
    // 1970-01-01 was a Thursday → mpl weekday index 3 (0=Monday).
    EXPECT_EQ(dates::civilFromNum(0.0f).weekday, 3);
    // 2024-06-03 was a Monday → 0.
    EXPECT_EQ(dates::civilFromNum(num(2024, 6, 3)).weekday, 0);
}

TEST(DateUnits, FractionalDaysBecomeClockTime) {
    auto c = dates::civilFromNum(num(2024, 1, 1) + 0.25f);
    EXPECT_EQ(c.hour, 6);
    EXPECT_EQ(c.minute, 0);
}

// ═══ Locators ═════════════════════════════════════════════════════════════

TEST(DateLocators, YearLocatorYearStarts) {
    dates::YearLocator loc{1};
    auto t = loc.tickValues(num(2020, 3, 1), num(2023, 8, 1));
    // Jan 1 2020 is before vmin — ticks are 2021, 2022, 2023.
    ASSERT_EQ(t.size(), 3u);
    auto c = dates::civilFromNum(t.front());
    EXPECT_EQ(c.year, 2021);
    EXPECT_EQ(c.month, 1u);
    EXPECT_EQ(c.day, 1u);
}

TEST(DateLocators, YearLocatorStep) {
    dates::YearLocator loc{5};
    auto t = loc.tickValues(num(1998, 6, 1), num(2021, 1, 1));
    // 5-aligned years in range: 2000, 2005, 2010, 2015, 2020
    ASSERT_EQ(t.size(), 5u);
    EXPECT_EQ(dates::civilFromNum(t[0]).year, 2000);
    EXPECT_EQ(dates::civilFromNum(t[4]).year, 2020);
}

TEST(DateLocators, MonthLocatorQuarterly) {
    dates::MonthLocator loc{3};
    auto t = loc.tickValues(num(2024, 1, 1), num(2024, 12, 31));
    ASSERT_EQ(t.size(), 4u);  // Jan, Apr, Jul, Oct
    EXPECT_EQ(dates::civilFromNum(t[1]).month, 4u);
}

TEST(DateLocators, DayLocatorMultiples) {
    dates::DayLocator loc{7};
    auto t = loc.tickValues(100.0f, 121.0f);
    ASSERT_EQ(t.size(), 3u);  // 105, 112, 119
    EXPECT_FLOAT_EQ(t[0], 105.0f);
    EXPECT_FLOAT_EQ(t[2], 119.0f);
}

TEST(DateLocators, WeekdayLocatorMondays) {
    dates::WeekdayLocator loc{1, 0};
    auto t = loc.tickValues(num(2024, 6, 3), num(2024, 6, 30));
    ASSERT_EQ(t.size(), 4u);  // Jun 3, 10, 17, 24
    EXPECT_EQ(dates::civilFromNum(t[0]).weekday, 0);
    EXPECT_EQ(dates::civilFromNum(t[0]).day, 3u);
}

TEST(DateLocators, HourLocator) {
    dates::HourLocator loc{6};
    auto t = loc.tickValues(10.0f, 11.0f);
    ASSERT_EQ(t.size(), 5u);  // 10.0, .25, .5, .75, 11.0
    EXPECT_FLOAT_EQ(t[1], 10.25f);
}

TEST(DateLocators, AutoDateLocatorYearScale) {
    dates::AutoDateLocator loc;
    auto t = loc.tickValues(num(2015, 1, 1), num(2025, 1, 1));
    ASSERT_GE(t.size(), 3u);
    EXPECT_LE(t.size(), 12u);
    // Year-scale ticks land on Jan 1.
    EXPECT_EQ(dates::civilFromNum(t.front()).month, 1u);
    EXPECT_EQ(dates::civilFromNum(t.front()).day, 1u);
}

TEST(DateLocators, AutoDateLocatorMonthScale) {
    dates::AutoDateLocator loc;
    auto t = loc.tickValues(num(2024, 1, 5), num(2024, 12, 20));
    ASSERT_GE(t.size(), 3u);
    // Month-scale ticks land on day 1.
    EXPECT_EQ(dates::civilFromNum(t.front()).day, 1u);
}

TEST(DateLocators, AutoDateLocatorSubDay) {
    dates::AutoDateLocator loc;
    // 2-hour span → minute ticks.
    auto t = loc.tickValues(20000.0f, 20000.0f + 2.0f / 24.0f);
    EXPECT_GE(t.size(), 3u);
    EXPECT_LE(t.size(), 20u);
}

// ═══ Formatters ═══════════════════════════════════════════════════════════

TEST(DateFormatters, StrftimeFormat) {
    dates::DateFormatter f{"%Y-%m-%d"};
    EXPECT_EQ(f.format(num(2024, 6, 15), 0), "2024-06-15");
    dates::DateFormatter fy{"%Y"};
    EXPECT_EQ(fy.format(num(1999, 7, 4), 0), "1999");
}

TEST(DateFormatters, AutoFormatterYearScale) {
    dates::AutoDateFormatter f;
    std::vector<float> locs = {num(2020, 1, 1), num(2021, 1, 1),
                               num(2022, 1, 1)};
    f.setLocs(locs);
    EXPECT_EQ(f.format(locs[0], 0), "2020");
}

TEST(DateFormatters, AutoFormatterSubDayAndBoundary) {
    dates::AutoDateFormatter f;
    std::vector<float> locs = {10.0f, 10.25f, 10.5f, 10.75f};
    f.setLocs(locs);
    EXPECT_EQ(f.format(10.25f, 1), "06:00");
    // Day-boundary tick shows the full date (mpl behavior).
    EXPECT_EQ(f.format(10.0f, 0), "1970-01-11");
}

TEST(DateFormatters, ConciseFormatter) {
    dates::ConciseDateFormatter f;
    std::vector<float> locs = {num(2024, 3, 1), num(2024, 3, 2),
                               num(2024, 3, 3)};
    f.setLocs(locs);
    EXPECT_EQ(f.format(locs[0], 0), "01");
    EXPECT_EQ(f.format(locs[1], 1), "02");
    // Larger context carried in the offset text.
    EXPECT_EQ(f.offsetText(), "Mar 2024");
}

TEST(DateFormatters, ConciseYearScaleNoOffset) {
    dates::ConciseDateFormatter f;
    std::vector<float> locs = {num(2020, 1, 1), num(2021, 1, 1),
                               num(2022, 1, 1)};
    f.setLocs(locs);
    EXPECT_EQ(f.format(locs[1], 1), "2021");
    EXPECT_TRUE(f.offsetText().empty());
}

// ═══ Units registry ═══════════════════════════════════════════════════════

TEST(UnitsRegistry, BuiltinsRegistered) {
    registerBuiltinConverters();
    EXPECT_NE(UnitsRegistry::instance().find<std::string>(), nullptr);
    EXPECT_NE(UnitsRegistry::instance().find<sys_days>(), nullptr);
    EXPECT_NE(UnitsRegistry::instance().find<sys_seconds>(), nullptr);
    EXPECT_EQ(UnitsRegistry::instance().find<float>(), nullptr);
}

TEST(UnitsRegistry, DateConverterConverts) {
    dates::DateConverter conv;
    EXPECT_FLOAT_EQ(conv.convert(ymd(1970, 1, 2), nullptr, 'x'), 1.0f);
    EXPECT_FLOAT_EQ(
        conv.convert(sys_seconds{seconds{86400 * 5}}, nullptr, 'x'),
        5.0f);
    EXPECT_FLOAT_EQ(conv.convert(42, nullptr, 'x'), 0.0f);  // unknown
}

TEST(UnitsRegistry, DateAxisInfoSuppliesAutoTicks) {
    dates::DateConverter conv;
    auto info = conv.axisInfo("x", nullptr);
    EXPECT_NE(dynamic_cast<dates::AutoDateLocator*>(info.locator.get()),
              nullptr);
    EXPECT_NE(
        dynamic_cast<dates::AutoDateFormatter*>(info.formatter.get()),
        nullptr);
}

// ═══ Categories ═══════════════════════════════════════════════════════════

TEST(CategoryUnits, CategoryIndexOrderOfAppearance) {
    Axes ax;
    EXPECT_EQ(ax.xCategoryIndex("b"), 0);
    EXPECT_EQ(ax.xCategoryIndex("a"), 1);
    EXPECT_EQ(ax.xCategoryIndex("b"), 0);  // stable
    EXPECT_EQ(ax.yCategoryIndex("q"), 0);  // per-axis state
    ASSERT_EQ(ax.xCategories().size(), 2u);
    EXPECT_EQ(ax.xCategories()[0], "b");
    EXPECT_EQ(ax.xCategories()[1], "a");
}

TEST(CategoryUnits, SetCategoriesInstallsFixedTicks) {
    Axes ax;
    ax.setXCategories({"q1", "q2", "q3", "q4"});
    auto* loc = dynamic_cast<FixedLocator*>(
        ax.style().xAxis.ticks.locator.get());
    ASSERT_NE(loc, nullptr);
    auto t = loc->tickValues(-1.0f, 5.0f);
    ASSERT_EQ(t.size(), 4u);
    EXPECT_FLOAT_EQ(t[2], 2.0f);
    auto* fmt = dynamic_cast<FixedFormatter*>(
        ax.style().xAxis.ticks.formatter.get());
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(fmt->format(2.0f, 2), "q3");
}

TEST(CategoryUnits, StrConverterAxisInfo) {
    Axes ax;
    ax.setYCategories({"low", "mid", "high"});
    dates::StrCategoryConverter conv;
    auto info = conv.axisInfo("y", &ax);
    ASSERT_NE(info.locator, nullptr);
    ASSERT_NE(info.formatter, nullptr);
    EXPECT_EQ(info.formatter->format(1.0f, 1), "mid");
}

TEST(CategoryUnits, PlotStringsConvertsAndLabels) {
    Axes ax;
    auto& lp = ax.plot(std::vector<std::string>{"a", "b", "c"},
                       std::vector<float>{1.0f, 2.0f, 3.0f});
    // Points mapped to 0,1,2.
    ASSERT_EQ(lp.series().points.size(), 3u);
    EXPECT_FLOAT_EQ(lp.series().points[0].x, 0.0f);
    EXPECT_FLOAT_EQ(lp.series().points[2].x, 2.0f);
    // Fixed ticks installed.
    EXPECT_NE(dynamic_cast<FixedLocator*>(
                  ax.style().xAxis.ticks.locator.get()),
              nullptr);
    EXPECT_EQ(ax.xCategories().size(), 3u);
}

TEST(CategoryUnits, PlotCategoriesOnY) {
    Axes ax;
    ax.plot(std::vector<float>{1.0f, 2.0f},
            std::vector<std::string>{"x", "y"});
    EXPECT_EQ(ax.yCategories().size(), 2u);
    EXPECT_NE(dynamic_cast<FixedFormatter*>(
                  ax.style().yAxis.ticks.formatter.get()),
              nullptr);
}

// ═══ Axes::plot unit conversion ═══════════════════════════════════════════

TEST(UnitsPlot, FloatXYPassesThrough) {
    Axes ax;
    auto& lp = ax.plot(std::vector<float>{0.0f, 1.0f},
                       std::vector<float>{5.0f, 7.0f});
    ASSERT_EQ(lp.series().points.size(), 2u);
    EXPECT_FLOAT_EQ(lp.series().points[1].y, 7.0f);
}

TEST(UnitsPlot, SysDaysInstallDateTicks) {
    Axes ax;
    std::vector<sys_days> d = {ymd(2024, 1, 1), ymd(2024, 1, 8),
                               ymd(2024, 1, 15)};
    auto& lp = ax.plot(d, std::vector<float>{1.0f, 2.0f, 1.5f});
    EXPECT_FLOAT_EQ(lp.series().points[1].x, num(2024, 1, 8));
    EXPECT_NE(dynamic_cast<dates::AutoDateLocator*>(
                  ax.style().xAxis.ticks.locator.get()),
              nullptr);
    EXPECT_NE(dynamic_cast<dates::AutoDateFormatter*>(
                  ax.style().xAxis.ticks.formatter.get()),
              nullptr);
}

TEST(UnitsPlot, XAxisDateManualInstall) {
    Axes ax;
    ax.xaxis_date();
    EXPECT_NE(dynamic_cast<dates::AutoDateLocator*>(
                  ax.style().xAxis.ticks.locator.get()),
              nullptr);
    ax.yaxis_date();
    EXPECT_NE(dynamic_cast<dates::AutoDateFormatter*>(
                  ax.style().yAxis.ticks.formatter.get()),
              nullptr);
}

// ═══ Rendering ════════════════════════════════════════════════════════════

TEST(CategoryRender, CategoricalLinePositions) {
    // Categories "a","b","c" → x = 0,1,2. The peak at "b" (x=1) should
    // land near the horizontal center of the canvas.
    CatFigure cf(256);
    auto& lp = cf.axes->plot(std::vector<std::string>{"a", "b", "c"},
                             std::vector<float>{0.2f, 0.9f, 0.2f});
    lp.series().color = Color::fromRgba8(255, 0, 0, 255);
    lp.series().lineWidth = 3.0f;
    auto img = cf.render();
    auto isRed = [](const Pixel& p) {
        return p.r > 150 && p.r > p.g + 40 && p.r > p.b + 40;
    };
    // Peak near column ~128, upper half.
    size_t centerRed = 0;
    for (uint32_t y = 10; y < 90; ++y)
        for (uint32_t x = 118; x < 138; ++x)
            if (isRed(img.get(x, y))) ++centerRed;
    EXPECT_GT(centerRed, 0u);
    // Endpoints near left/right edges, lower half.
    size_t edgeRed = 0;
    for (uint32_t y = 170; y < 230; ++y)
        for (uint32_t x = 0; x < 40; ++x)
            if (isRed(img.get(x, y))) ++edgeRed;
    EXPECT_GT(edgeRed, 0u);
}

TEST(DateRender, DateAxisRendersLine) {
    CatFigure cf(256);
    std::vector<sys_days> d = {ymd(2024, 1, 1), ymd(2024, 6, 1),
                               ymd(2024, 12, 1)};
    auto& lp = cf.axes->plot(d, std::vector<float>{0.2f, 0.8f, 0.5f});
    lp.series().color = Color::fromRgba8(255, 0, 0, 255);
    lp.series().lineWidth = 3.0f;
    auto img = cf.render();
    size_t red = 0;
    for (uint32_t y = 0; y < img.height(); ++y)
        for (uint32_t x = 0; x < img.width(); ++x) {
            auto p = img.get(x, y);
            if (p.r > 150 && p.r > p.g + 40 && p.r > p.b + 40) ++red;
        }
    EXPECT_GT(red, 100u);  // a diagonal-ish line across ~half the canvas
}
