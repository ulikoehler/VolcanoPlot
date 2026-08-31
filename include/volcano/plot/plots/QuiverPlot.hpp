// volcano/plot/plots/QuiverPlot.hpp — vector field plot (matplotlib `quiver`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for QuiverPlot.
struct QuiverConfig {
    Color color = Color::black();
    float lineWidth = 1.0f;
    /// Scale factor for arrow lengths. 0 = auto (fit to grid spacing).
    float scale = 0.0f;
    /// Arrowhead length in pixels.
    float headLength = 8.0f;
    /// Arrowhead width in pixels.
    float headWidth = 6.0f;
    /// If true, arrowheads are drawn as filled triangles.
    bool filledHeads = true;
    std::string label;
};

/// Quiver (vector field) plot. Draws arrows at grid positions representing
/// a 2D vector field. Equivalent to matplotlib's `quiver(X, Y, U, V)`.
class QuiverPlot : public IPlot {
public:
    /// Construct from explicit positions and vectors.
    /// x, y: arrow start positions. u, v: arrow components.
    QuiverPlot(std::vector<float> x, std::vector<float> y,
               std::vector<float> u, std::vector<float> v,
               QuiverConfig cfg = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return cfg_.label; }
    [[nodiscard]] Color legendColor() const override { return cfg_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

private:
    std::vector<float> x_, y_, u_, v_;
    QuiverConfig cfg_;

    // Geometry computed in prepare() — in pixel space for arrowheads,
    // but shafts are in data space.
    std::vector<Point2D> shaftSegs_;     // data space line segments
    std::vector<Point2D> headFillPos_;   // pixel space triangle positions
    std::vector<Color> headFillColors_;  // per-vertex colors

    render::primitives::LineSegmentRenderer shaftRenderer_;
    render::primitives::FillRenderer headRenderer_;
    bool prepared_ = false;

    // For draw(), we need to recompute arrowheads per-frame in pixel space.
    // We store the shaft endpoints and arrowhead params, then draw heads
    // via SpineRenderer's drawLineStrip or a separate approach.
    // Actually, we'll compute arrowheads in data space in prepare() using
    // the axes viewport at that time. But viewport may change...
    // Simpler: compute everything in draw() using the current viewport.
    bool geometryBuilt_ = false;
    void buildGeometry(const Axes& axes, Rect2D rect);
};

} // namespace volcano::plot
