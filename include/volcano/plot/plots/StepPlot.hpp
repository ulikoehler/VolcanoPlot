// volcano/plot/plots/StepPlot.hpp — step plot (matplotlib `step`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>

namespace volcano::plot {

/// Step draw style, matching matplotlib's `where` parameter.
enum class StepWhere {
    Pre,  ///< Step before the x value (horizontal then vertical)
    Post, ///< Step after the x value (vertical then horizontal)
    Mid,  ///< Step at the midpoint between consecutive x values
};

/// Step plot — renders a line as a staircase.
/// Equivalent to matplotlib's `step(x, y, where='pre'|'post'|'mid')`.
class StepPlot : public IPlot {
public:
    StepPlot(std::vector<float> x, std::vector<float> y,
             StepWhere where = StepWhere::Pre,
             Color color = Color::blue(), float lineWidth = 1.5f);

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    std::vector<float> x_, y_;
    StepWhere where_;
    Color color_;
    float lineWidth_;
    std::string label_;
    std::vector<Point2D> stepPoints_;  // expanded staircase points
    render::primitives::LineRenderer renderer_;
    bool prepared_ = false;

    void buildStepPoints();
};

/// Stairs plot — renders a step function as a stair outline.
/// Equivalent to matplotlib's `stairs(values, edges)`.
/// Unlike StepPlot, stairs takes explicit bin edges and values.
class StairsPlot : public IPlot {
public:
    /// Construct from values and bin edges.
    /// `values` has N elements, `edges` has N+1 elements.
    StairsPlot(std::vector<float> values, std::vector<float> edges,
               Color color = Color::blue(), float lineWidth = 1.5f,
               bool fill = false,
               Color fillColor = Color::fromRgba8(31, 119, 180, 128));

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    void setLabel(std::string l) { label_ = std::move(l); }

private:
    std::vector<float> values_, edges_;
    Color color_, fillColor_;
    float lineWidth_;
    bool fill_;
    std::string label_;
    std::vector<Point2D> stepPoints_;  // staircase outline
    render::primitives::LineRenderer lineRenderer_;
    render::primitives::FillRenderer fillRenderer_;
    std::vector<Point2D> fillPositions_;
    std::vector<Color> fillColors_;
    bool prepared_ = false;
};

} // namespace volcano::plot
