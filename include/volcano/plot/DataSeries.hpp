// volcano/plot/DataSeries.hpp — data series abstraction
#pragma once

#include "volcano/plot/Types.hpp"

#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace volcano::plot {

/// A series of 2D points (scatter, line, etc.).
struct Series2D {
    std::vector<Point2D> points;
    std::string label;
    Color color = Color::blue();
    float size = 6.0f;          // marker size in pixels
    MarkerStyle marker = MarkerStyle::Circle;
    LineStyle lineStyle = LineStyle::Solid;
    float lineWidth = 1.5f;
};

/// A series of 3D points (3D scatter, surface).
struct Series3D {
    std::vector<Point3D> points;
    std::string label;
    Color color = Color::blue();
    float size = 6.0f;
};

/// A 2D grid of scalar values (heatmap, surface, KDE).
struct Grid2D {
    std::vector<float> values; // width*height, row-major
    uint32_t width = 0;
    uint32_t height = 0;
    Range xRange{0,1};
    Range yRange{0,1};
    /// Optional value range; if invalid, computed from data.
    Range valueRange{0,1};
};

/// A function to be evaluated on the GPU.
struct FunctionEval {
    /// GLSL expression body, e.g. "sin(x)*cos(y)".
    /// The variables x (and y for 2D) are available.
    std::string glslBody;
    /// For 1D functions: number of samples to evaluate.
    uint32_t samples = 1024;
    Range xRange{-10, 10};
    Range yRange{-10, 10}; // for 2D functions
    bool is2D = false;
};

/// Bar chart data.
struct BarData {
    std::vector<float> heights;
    std::vector<std::string> labels;
    std::vector<Color> colors; // optional per-bar colors
    float width = 0.8f;
    bool horizontal = false;
};

/// Pie chart data.
struct PieData {
    std::vector<float> values;
    std::vector<std::string> labels;
    std::vector<Color> colors;
    bool donut = false;       // 3D pie / donut
    float innerRadius = 0.0f; // for donut
    float explode = 0.0f;
};

/// Variant of all data kinds.
using DataVariant = std::variant<Series2D, Series3D, Grid2D, FunctionEval, BarData, PieData>;

} // namespace volcano::plot
