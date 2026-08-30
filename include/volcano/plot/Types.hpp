// volcano/plot/Types.hpp — common plot types
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

struct Point2D { float x; float y; };
struct Point3D { float x; float y; float z; };

/// 2D extent (width, height) in pixels.
struct Extent2D { uint32_t width = 0; uint32_t height = 0; };

/// 2D rectangle (offset + extent) in pixels.
struct Rect2D { int32_t x = 0; int32_t y = 0; uint32_t width = 0; uint32_t height = 0; };

/// RGBA color, normalized [0,1].
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    static constexpr Color black()   { return {0,0,0,1}; }
    static constexpr Color white()   { return {1,1,1,1}; }
    static constexpr Color red()     { return {1,0,0,1}; }
    static constexpr Color green()   { return {0,1,0,1}; }
    static constexpr Color blue()    { return {0,0,1,1}; }
    static constexpr Color transparent() { return {0,0,0,0}; }
    /// Construct from 8-bit RGBA.
    static constexpr Color fromRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r/255.0f, g/255.0f, b/255.0f, a/255.0f};
    }

    /// Parse a matplotlib-style color string. Supports:
    ///   - Single-letter shorthands: "b", "g", "r", "c", "m", "y", "k", "w"
    ///   - Hex: "#RGB", "#RRGGBB", "#RGBA", "#RRGGBBAA"
    ///   - Named CSS4 colors: "red", "blue", "lightblue", etc.
    ///   - CN cycle colors: "C0"–"C9" (uses the default color cycle)
    ///   - Grayscale: "0.0"–"1.0" (string of a float in [0,1])
    /// Returns nullopt on parse failure.
    static std::optional<Color> parse(std::string_view s);

    /// Same as parse(), but returns black on failure (for convenience).
    static Color parseOr(std::string_view s, Color fallback = black());
};

/// Default color cycle (matplotlib's "tab10" palette).
/// Used by "C0"–"C9" color strings and for automatic plot coloring.
class ColorCycle {
public:
    /// Get the color at index i (wraps around modulo 10).
    [[nodiscard]] static Color at(size_t i);
    /// Number of colors in the default cycle.
    static constexpr size_t size() { return 10; }
};

/// Axis range in data coordinates.
struct Range {
    float min = 0.0f;
    float max = 1.0f;
    [[nodiscard]] float span() const noexcept { return max - min; }
    [[nodiscard]] bool valid() const noexcept { return max > min; }
};

/// 2D viewport in data coordinates.
struct Viewport {
    Range x{0,1};
    Range y{0,1};
    Range z{0,1}; // for 3D
};

/// Marker style for scatter plots.
enum class MarkerStyle {
    Circle, Square, Diamond, Triangle, Plus, X, Star, Point
};

/// Line style.
enum class LineStyle {
    Solid, Dashed, Dotted, DashDot, None
};

} // namespace volcano::plot
