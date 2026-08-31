// volcano/plot/plots/Errorbar3D.hpp — 3D error bar plot (matplotlib `errorbar` in 3D)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for Errorbar3D.
struct Errorbar3DConfig {
    /// Symmetric errors (±value per point).
    std::vector<float> xerr, yerr, zerr;
    /// Asymmetric errors (lower/upper per point).
    std::vector<float> xerrLower, xerrUpper;
    std::vector<float> yerrLower, yerrUpper;
    std::vector<float> zerrLower, zerrUpper;
    /// Marker color.
    Color markerColor = Color::fromRgba8(31, 119, 180, 255);
    /// Error bar color.
    Color errorbarColor = Color::black();
    /// Marker size in pixels.
    float markerSize = 6.0f;
    /// Error bar line width.
    float errorbarWidth = 1.0f;
    /// Cap size in pixels.
    float capSize = 5.0f;
    /// Whether to draw markers.
    bool drawMarker = true;
    /// Whether to draw caps.
    bool drawCaps = true;
    /// Label for legend.
    std::string label;
};

/// 3D error bar plot — renders points in 3D space with optional error bars
/// along the x, y, and z axes. Equivalent to matplotlib's `Axes3D.errorbar`.
///
/// The 3D points and error bar endpoints are projected to 2D NDC on the CPU
/// using the Camera3D view-projection matrix. Error bars are rendered as
/// line segments via LineSegmentRenderer, and markers via PointRenderer.
/// Caps are short perpendicular line segments at the error bar ends.
class Errorbar3D : public IPlot {
public:
    /// Construct from (x, y, z) arrays and configuration.
    Errorbar3D(std::vector<float> x, std::vector<float> y, std::vector<float> z,
               Errorbar3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.markerColor; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

private:
    std::vector<float> x_, y_, z_;
    Errorbar3DConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer errorRenderer_;
    std::vector<Point2D> errorSegments_;

    render::primitives::PointRenderer pointRenderer_;
    std::vector<Point2D> markerPoints_;
    std::vector<Color> markerColors_;
    std::vector<float> markerSizes_;

    bool prepared_ = false;
    bool hasErrors_ = false;

    void projectGeometry();
    /// Get effective error bounds for point i along an axis.
    /// Returns (lower, upper) offsets from the data point.
    void errBounds(size_t i, const std::vector<float>& sym,
                   const std::vector<float>& lower,
                   const std::vector<float>& upper,
                   float& lo, float& hi) const;
};

} // namespace volcano::plot
