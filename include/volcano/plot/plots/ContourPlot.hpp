// volcano/plot/plots/ContourPlot.hpp — contour and contourf plots
// Isolines and filled bands of a 2D scalar field via CPU marching squares.
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for contour and contourf plots.
/// Defined outside the class to avoid nested-struct default-member-initializer
/// build issues (see ErrorbarConfig, BoxPlotConfig).
struct ContourConfig {
    /// Explicit contour levels. If empty, levels are auto-computed.
    std::vector<float> levels;
    /// Number of auto levels if `levels` is empty.
    int numLevels = 10;
    /// Colormap for coloring. If nullptr, contour lines use `lineColor`
    /// and filled bands use a grayscale ramp.
    const Colormap* cmap = nullptr;
    /// Color for contour lines when no colormap is set.
    Color lineColor = Color::black();
    /// Line width for contour lines.
    float lineWidth = 1.0f;
    /// matplotlib `clabel`: draw level-value labels on the contour lines.
    bool clabel = false;
    /// Label text scale for clabel (TextRenderer scale, ~0.6 default size).
    float clabelFontScale = 0.45f;
    /// Label color (default: lineColor).
    Color clabelColor = Color{0, 0, 0, 0};   // alpha 0 → use lineColor
    /// Which levels get labels (empty → all levels).
    std::vector<float> clabelLevels;
};

/// Contour plot — isolines of a 2D scalar field.
/// Uses CPU marching squares to generate line segments for each contour
/// level, rendered via LineSegmentRenderer with a uniform color.
class ContourPlot : public IPlot {
public:
    ContourPlot(Grid2D grid, ContourConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return config_.lineColor; }
    void setLabel(std::string l) { label_ = std::move(l); }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

private:
    Grid2D grid_;
    ContourConfig config_;
    std::string label_;
    render::primitives::LineSegmentRenderer renderer_;
    std::vector<Point2D> segments_;  // computed in prepare()
    std::vector<float> segLevels_;   // level of each segment pair (size = segments_/2)
    bool prepared_ = false;

    void computeLevels();
    void marchingSquares();
    /// Draw clabel text at the representative midpoint of each level.
    void drawClabels(vk::CommandBuffer cmd, render::Renderer& r,
                     const Axes& axes, Rect2D rect);
};

/// Filled contour plot — filled bands between contour levels.
/// Uses CPU marching squares with Sutherland-Hodgman polygon clipping
/// to generate filled polygons for each level band, rendered via
/// FillRenderer with per-vertex colors from the colormap.
class ContourfPlot : public IPlot {
public:
    ContourfPlot(Grid2D grid, ContourConfig config = {});

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override;
    void setLabel(std::string l) { label_ = std::move(l); }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

private:
    Grid2D grid_;
    ContourConfig config_;
    std::string label_;
    render::primitives::FillRenderer renderer_;
    std::vector<Point2D> positions_;  // triangle vertices
    std::vector<Color> colors_;       // per-vertex colors
    bool prepared_ = false;

    void computeLevels();
    void marchingSquaresFilled();
};

} // namespace volcano::plot
