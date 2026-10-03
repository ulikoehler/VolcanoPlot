// volcano/plot/plots/TricontourPlot.hpp — 3D tricontour and tricontourf plots
// (matplotlib `Axes3D.tricontour` / `Axes3D.tricontourf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include <map>
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
    /// mpl offset=None semantics: draw each contour at its own z —
    /// lines at `level`, filled bands at the band midpoint.
    /// Overrides zLevel/zOffset when true.
    bool levelsAsZ = false;
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
    [[nodiscard]] bool is3D() const override { return true; }
    /// Construct from (x, y, z) arrays. z is the scalar field value.
    TricontourPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, TricontourConfig config = {});

    /// Construct with explicit triangles.
    TricontourPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, std::vector<Triangle> triangles,
                   TricontourConfig config = {});

    void setCamera(const Camera3D& camera) { camera_ = camera; }
    Camera3D* camera3D() noexcept override { return &camera_; }

    void prepare(render::Renderer& r) override;
    void draw(render::Cmd& cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.lineColor; }

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> triangles_;
    TricontourConfig config_;
    Camera3D camera_;

    std::unique_ptr<render::primitives::LineSegmentRenderer> renderer_;
    /// Raw world-space segment endpoints — projected by the renderer.
    std::vector<Point3D> segments_;
    std::array<float, 16> vp_{};
    /// Contour level per segment pair (segments_[2i], segments_[2i+1])
    /// — used to color lines per level like mpl contour.
    std::vector<float> segLevels_;
    /// Lazily-created renderer per level when a colormap is set.
    std::map<float, std::pair<
        std::unique_ptr<render::primitives::LineSegmentRenderer>,
        std::vector<Point3D>>> byLevel_;
    bool prepared_ = false;

    void computeLevels();
    void extractContours();
};

/// 3D filled tricontour plot — filled bands between contour levels on
/// scattered data, drawn at a fixed z-level in 3D space.
/// Equivalent to matplotlib's `Axes3D.tricontourf`.
class TricontourfPlot : public IPlot {
public:
    [[nodiscard]] bool is3D() const override { return true; }
    TricontourfPlot(std::vector<float> x, std::vector<float> y,
                    std::vector<float> z, TricontourConfig config = {});

    TricontourfPlot(std::vector<float> x, std::vector<float> y,
                    std::vector<float> z, std::vector<Triangle> triangles,
                    TricontourConfig config = {});

    void setCamera(const Camera3D& camera) { camera_ = camera; }
    Camera3D* camera3D() noexcept override { return &camera_; }

    void prepare(render::Renderer& r) override;
    void draw(render::Cmd& cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, z_;
    std::vector<Triangle> triangles_;
    TricontourConfig config_;
    Camera3D camera_;

    std::unique_ptr<render::primitives::FillRenderer> renderer_;
    /// Raw world-space triangle soup — projected by the renderer.
    std::vector<Point3D> positions_;
    std::vector<Color> colors_;
    std::array<float, 16> vpf_{};
    bool prepared_ = false;

    void computeLevels();
    void extractContoursFilled();
};

} // namespace volcano::plot
