// volcano/plot/Units.cpp — units registry + built-in converters
#include "volcano/plot/Units.hpp"
#include "volcano/plot/Dates.hpp"

#include <chrono>
#include <string>

namespace volcano::plot {

UnitsRegistry& UnitsRegistry::instance() {
    static UnitsRegistry r;
    return r;
}

void registerBuiltinConverters() {
    static bool done = [] {
        auto& reg = UnitsRegistry::instance();
        reg.add<std::chrono::sys_days, dates::DateConverter>();
        reg.add<std::chrono::sys_seconds, dates::DateConverter>();
        reg.add<std::chrono::sys_time<std::chrono::milliseconds>,
                dates::DateConverter>();
        reg.add<std::chrono::sys_time<std::chrono::microseconds>,
                dates::DateConverter>();
        reg.add<std::chrono::system_clock::time_point,
                dates::DateConverter>();
        reg.add<std::string, dates::StrCategoryConverter>();
        return true;
    }();
    (void)done;
}

} // namespace volcano::plot
