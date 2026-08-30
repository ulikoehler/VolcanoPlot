// volcano/plot/plots/Contour3D.hpp — 3D contour and contourf plots
// (matplotlib `Axes3D.contour` / `Axes3D.contourf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for 3D contour and contourf plots.
struct Contour3DConfig {
    /// Explicit contour levels. If empty, levels are auto-computed.
    std::vector<float> levels;
    /// Number of auto levels if `levels` is empty.
    int numLevels = 10;
    /// Colormap for coloring.
    const Colormap* cmap = nullptr;
    /// Color for contour lines when no colormap is set.
    Color lineColor = Color::black();
    /// Line width for contour lines.
    float lineWidth = 1.0f;
    /// Z-level at which the contour is drawn (default: zmin of data).
    float zLevel = 0.0f;
    /// Whether to use offset mode (zLevel is relative to zmin).
    bool zOffset = true;
    /// Label for legend.
    std::string label;
};

/// 3D contour plot — isolines of a 2D scalar field drawn at a fixed z-level
/// in 3D space. Equivalent to matplotlib's `Axes3D.contour`.
///
/// Uses CPU marching squares to generate contour line segments in 2D (x, y)
/// space, then places them at a fixed z-level and projects through Camera3D
/// to 2D NDC. Rendered via LineSegmentRenderer.
class Contour3D : public IPlot {
public:
    Contour3D(Grid2D grid, Contour3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.lineColor; }

private:
    Grid2D grid_;
    Contour3DConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> segments_;
    bool prepared_ = false;

    void computeLevels();
    void marchingSquares();
};

/// 3D filled contour plot — filled bands between contour levels drawn at a
/// fixed z-level in 3D space. Equivalent to matplotlib's `Axes3D.contourf`.
class Contourf3D : public IPlot {
public:
    Contourf3D(Grid2D grid, Contour3DConfig config = {});

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    Grid2D grid_;
    Contour3DConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> positions_;
    std::vector<Color> colors_;
    bool prepared_ = false;

    void computeLevels();
    void marchingSquaresFilled();
};

} // namespace volcano::plot
