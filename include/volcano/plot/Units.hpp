// volcano/plot/Units.hpp — matplotlib-style unit conversion registry
//
// Mirrors `matplotlib.units`: a UnitConverter turns unit-typed data
// (dates, category strings, ...) into axis floats and supplies default
// tick locators/formatters via `axisInfo`. `UnitsRegistry::instance()`
// is the global type → converter map (mpl `units.registry`).
#pragma once

#include "volcano/plot/Ticks.hpp"

#include <any>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace volcano::plot {

class Axes;

/// mpl `AxisInfo`: the defaults a unit supplies to an axis.
struct AxisInfo {
    std::shared_ptr<Locator> locator;        ///< major locator
    std::shared_ptr<Locator> minorLocator;   ///< minor locator
    std::shared_ptr<Formatter> formatter;    ///< major formatter
    std::shared_ptr<Formatter> minorFormatter;
    std::string label;                       ///< default axis label
};

/// mpl `ConversionInterface`: converts unit-typed values to floats.
/// `axis` is 'x' or 'y' — stateful converters (e.g. categories) keep
/// per-axis state on `ax` (may be nullptr for pure conversions).
class UnitConverter {
public:
    virtual ~UnitConverter() = default;

    /// Convert one unit-typed value into an axis coordinate.
    [[nodiscard]] virtual float convert(const std::any& value, Axes* ax,
                                        char axis) const = 0;

    /// Convert a sequence; default maps `convert` element-wise.
    template <class T>
    [[nodiscard]] std::vector<float>
    convertSeq(const std::vector<T>& values, Axes* ax, char axis) const {
        std::vector<float> out;
        out.reserve(values.size());
        for (const auto& v : values) out.push_back(convert(v, ax, axis));
        return out;
    }

    /// mpl `axisinfo(unit, axis)`: default locators/formatters/label.
    /// `which` is "x" or "y" so stateful converters can read the right
    /// per-axis unit data.
    [[nodiscard]] virtual AxisInfo axisInfo(std::string_view which,
                                            const Axes* ax) const {
        (void)which; (void)ax;
        return {};
    }
};

/// mpl `units.registry`: maps a C++ data type to its converter.
class UnitsRegistry {
public:
    /// Global registry (mpl's module-level `units.registry`).
    [[nodiscard]] static UnitsRegistry& instance();

    /// Register `conv` for data type `type` (mpl `registry[T] = conv`).
    void add(std::type_index type, std::shared_ptr<UnitConverter> conv) {
        map_[type] = std::move(conv);
    }
    template <class T>
    void add(std::shared_ptr<UnitConverter> conv) {
        add(std::type_index(typeid(T)), std::move(conv));
    }
    /// Register a default-constructed converter for `T`.
    template <class T, class Conv>
    void add() { add<T>(std::make_shared<Conv>()); }

    /// Converter for `type`, or nullptr (mpl `registry.get(T)`).
    [[nodiscard]] UnitConverter* find(std::type_index type) const {
        auto it = map_.find(type);
        return it != map_.end() ? it->second.get() : nullptr;
    }
    template <class T>
    [[nodiscard]] UnitConverter* find() const {
        return find(std::type_index(typeid(T)));
    }

    /// mpl `registry.clear()`.
    void clear() { map_.clear(); }

private:
    std::unordered_map<std::type_index, std::shared_ptr<UnitConverter>>
        map_;
};

/// Register the built-in converters (dates, string categories). Called
/// lazily by the Axes unit helpers; safe to call repeatedly.
void registerBuiltinConverters();

/// Convert a typed sequence through the registry; falls back to a
/// static_cast<float> for plain numeric types. `axis` is 'x' or 'y'.
template <class T>
std::vector<float> convertSeq(const std::vector<T>& values, Axes* ax,
                              char axis) {
    if constexpr (std::is_convertible_v<T, float>) {
        std::vector<float> out(values.begin(), values.end());
        return out;
    } else {
        auto* conv = UnitsRegistry::instance().find<T>();
        std::vector<float> out;
        out.reserve(values.size());
        for (const auto& v : values)
            out.push_back(conv ? conv->convert(v, ax, axis) : 0.0f);
        return out;
    }
}

} // namespace volcano::plot
