// volcano/plot/plots/TricontourPlot.hpp — 3D tricontour and tricontourf plots
// (matplotlib `Axes3D.tricontour` / `Axes3D.tricontourf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for 3D tricontour and tricontourf plots.
struct TricontourConfig {
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

/// 3D tricontour plot — isolines of a scalar field defined on scattered
/// points, drawn at a fixed z-level in 3D space.
/// Equivalent to matplotlib's `Axes3D.tricontour`.
///
/// The (x, y) coordinates are Delaunay-triangulated, and z is used as the
/// scalar field value. Contour line segments are extracted by checking
/// which triangle edges cross each level, then projected through Camera3D
/// to 2D NDC. Rendered via LineSegmentRenderer.
class TricontourPlot : public IPlot {
public:
    /// Construct from (x, y, z) arrays. z is the scalar field value.
    TricontourPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, TricontourConfig config = {});

    /// Construct with explicit triangles.
    TricontourPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, std::vector<Triangle> triangles,
                   TricontourConfig config = {});

    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.lineColor; }

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> triangles_;
    TricontourConfig config_;
    Camera3D camera_;

    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> segments_;
    bool prepared_ = false;

    void computeLevels();
    void extractContours();
};

/// 3D filled tricontour plot — filled bands between contour levels on
/// scattered data, drawn at a fixed z-level in 3D space.
/// Equivalent to matplotlib's `Axes3D.tricontourf`.
class TricontourfPlot : public IPlot {
public:
    TricontourfPlot(std::vector<float> x, std::vector<float> y,
                    std::vector<float> z, TricontourConfig config = {});

    TricontourfPlot(std::vector<float> x, std::vector<float> y,
                    std::vector<float> z, std::vector<Triangle> triangles,
                    TricontourConfig config = {});

    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> triangles_;
    TricontourConfig config_;
    Camera3D camera_;

    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> positions_;
    std::vector<Color> colors_;
    bool prepared_ = false;

    void computeLevels();
    void extractContoursFilled();
};

} // namespace volcano::plot
