// volcano/plot/plots/FunctionPlot.hpp — GPU-side function evaluation plot
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
namespace volcano::plot {

/// Plots a function y = f(x) by evaluating it on the GPU via a compute shader.
/// The GLSL body is compiled at runtime (requires shaderc).
/// Supports infinite zoom: sample count is proportional to canvas width.
class FunctionPlot : public IPlot {
public:
    FunctionPlot(std::string glslBody, Range xRange, uint32_t samples = 1024,
                 Color color = Color::blue(), float lineWidth = 1.5f,
                 std::string label = "f(x)")
        : glslBody_(std::move(glslBody)), xRange_(xRange), samples_(samples),
          color_(color), lineWidth_(lineWidth), label_(std::move(label)) {}

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] Color legendColor() const override { return color_; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

    /// Re-evaluate the function on the GPU for the current viewport.
    /// Called when the viewport changes (infinite zoom).
    void reevaluate(render::Renderer& r, Range xRange, uint32_t canvasWidth);

private:
    std::string glslBody_;
    Range xRange_;
    uint32_t samples_;
    Color color_;
    float lineWidth_;
    std::string label_;
    std::vector<Point2D> points_;
    render::primitives::LineRenderer renderer_;
    bool prepared_ = false;
};

} // namespace volcano::plot
