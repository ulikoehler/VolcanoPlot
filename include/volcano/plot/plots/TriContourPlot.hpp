// volcano/plot/plots/TriContourPlot.hpp — contour plots on unstructured
// triangular grids (matplotlib `tricontour`, `tricontourf`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for tricontour and tricontourf plots.
struct TriContourConfig {
    /// Explicit contour levels. If empty, levels are auto-computed.
    std::vector<float> levels;
    /// Number of auto levels if `levels` is empty.
    int numLevels = 10;
    /// Colormap for coloring. If nullptr, contour lines use `lineColor`.
    const Colormap* cmap = nullptr;
    /// Color for contour lines when no colormap is set.
    Color lineColor = Color::black();
    /// Line width for contour lines.
    float lineWidth = 1.0f;
    /// Label for legend.
    std::string label;
};

/// Contour plot on unstructured triangular grids.
/// Delaunay-triangulates the (x,y) points, then extracts isolines via
/// marching triangles. Equivalent to matplotlib's `tricontour`.
class TriContourPlot : public IPlot {
public:
    /// Construct from unstructured (x, y, z) points.
    TriContourPlot(std::vector<float> x, std::vector<float> y,
                   std::vector<float> z, TriContourConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.lineColor; }

private:
    std::vector<float> x_, y_, z_;
    TriContourConfig config_;
    std::vector<Triangle> tris_;
    std::vector<float> levels_;
    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> segments_;
    bool prepared_ = false;

    void computeLevels();
    void marchingTriangles();
};

/// Filled contour plot on unstructured triangular grids.
/// Equivalent to matplotlib's `tricontourf`.
class TriContourfPlot : public IPlot {
public:
    /// Construct from unstructured (x, y, z) points.
    TriContourfPlot(std::vector<float> x, std::vector<float> y,
                    std::vector<float> z, TriContourConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override;

private:
    std::vector<float> x_, y_, z_;
    TriContourConfig config_;
    std::vector<Triangle> tris_;
    std::vector<float> levels_;
    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> positions_;
    std::vector<Color> colors_;
    bool prepared_ = false;

    void computeLevels();
    void marchingTrianglesFilled();
};

} // namespace volcano::plot
