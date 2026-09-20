// volcano/plot/plots/Axes3DPlot.hpp — mplot3d-style axes box
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"

namespace volcano::plot {

/// Configuration for the 3D axes box (matplotlib mplot3d panes).
struct Axes3DConfig {
    /// Pane face color (matplotlib default pane color ~0.95 gray).
    Color paneColor = Color::fromRgba8(242, 242, 242, 255);
    /// Box edge / pane border color.
    Color edgeColor = Color::fromRgba8(160, 160, 160, 255);
    /// Axis line + tick mark color (the three edges carrying ticks;
    /// matplotlib draws these darker than the pane edges).
    Color axisColor = Color::black();
    /// Pane gridline color.
    Color gridColor = Color::fromRgba8(178, 178, 178, 255);
    /// Tick label color.
    Color labelColor = Color::black();
    /// Edge/grid line width (px).
    float lineWidth = 1.0f;
    /// Draw pane fills, pane gridlines, box edges, tick labels.
    bool panes = true;
    bool grid = true;
    bool edges = true;
    bool tickLabels = true;
    /// Number of tick bins per axis (matplotlib AutoLocator nbins=9).
    int tickBins = 9;
    /// Tick mark length and label offset, in NDC units (~2/axes-span).
    float tickSize = 0.018f;
    float labelPad = 0.05f;
};

/// Draws the matplotlib Axes3D box: three pane faces (light gray), pane
/// gridlines at tick positions, the 12 box edges, and tick labels along
/// the three outer axis edges. Everything is CPU-projected through the
/// Camera3D view-projection, then rendered with 2D primitives (like
/// WireframePlot). Add it before the 3D data plots so panes sit behind
/// the data; give it a low zorder.
class Axes3DPlot : public IPlot {
public:
    Axes3DPlot(Camera3D camera, Viewport dataRange,
               Axes3DConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] bool is3D() const override { return true; }
    Camera3D* camera3D() noexcept override { return &camera_; }

private:
    struct TickLabel { float x, y; std::string text; };

    Camera3D camera_;
    Viewport range_;
    Axes3DConfig config_;
    std::vector<Point2D> paneTris_;
    std::vector<Point2D> lineSegs_;
    std::vector<Point2D> axisSegs_;  // tick edges + tick marks (axisColor)
    std::vector<TickLabel> labels_;
    render::primitives::FillRenderer fillRenderer_;
    render::primitives::LineSegmentRenderer lineRenderer_;
    render::primitives::LineSegmentRenderer axisRenderer_;
    bool prepared_ = false;
};

} // namespace volcano::plot
