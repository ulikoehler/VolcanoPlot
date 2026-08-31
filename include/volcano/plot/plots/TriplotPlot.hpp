// volcano/plot/plots/TriplotPlot.hpp — draw triangulation edges
// (matplotlib `triplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Triangulation.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Triplot configuration.
struct TriplotConfig {
    /// Edge line color.
    Color color = Color::fromRgba8(0, 0, 0, 128);  // semi-transparent black
    /// Edge line width.
    float lineWidth = 1.0f;
    /// Whether to draw markers at vertices.
    bool showMarkers = false;
    /// Marker color.
    Color markerColor = Color::black();
    /// Marker size in pixels.
    float markerSize = 4.0f;
    /// Label for legend.
    std::string label;
};

/// Triangulation plot — draws the edges of an unstructured triangular grid.
/// Equivalent to matplotlib's `triplot`.
///
/// Delaunay-triangulates the (x,y) points (or uses explicit triangles),
/// then draws each triangle edge as a line segment. Optionally draws
/// markers at the vertices.
class TriplotPlot : public IPlot {
public:
    /// Construct from (x, y) points — Delaunay triangulation is computed.
    TriplotPlot(std::vector<float> x, std::vector<float> y,
                TriplotConfig config = {});

    /// Construct from explicit triangles.
    TriplotPlot(std::vector<float> x, std::vector<float> y,
                std::vector<Triangle> triangles,
                TriplotConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

private:
    std::vector<float> x_, y_;
    std::vector<Triangle> tris_;
    TriplotConfig config_;

    render::primitives::LineSegmentRenderer lineRenderer_;
    std::vector<Point2D> segments_;

    render::primitives::PointRenderer pointRenderer_;
    std::vector<Point2D> points_;
    std::vector<Color> pointColors_;

    bool prepared_ = false;

    void buildEdges();
};

} // namespace volcano::plot
