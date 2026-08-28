// volcano/plot/Types.hpp — common plot types
#pragma once

#include <cstdint>
#include <string>
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
