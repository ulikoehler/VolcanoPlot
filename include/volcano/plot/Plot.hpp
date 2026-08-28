// volcano/plot/Plot.hpp — IPlot interface + Figure
#pragma once

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Style.hpp"

#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>
#include <vector>

namespace volcano::render { class Renderer; }

namespace volcano::plot {

/// Interface implemented by all plot types (scatter, line, bar, pie, ...).
class IPlot {
public:
    virtual ~IPlot() = default;
    /// Called once to upload GPU resources (buffers, pipelines).
    virtual void prepare(render::Renderer& renderer) = 0;
    /// Called every frame to record draw commands.
    virtual void draw(vk::CommandBuffer cmd, render::Renderer& renderer,
                      const Axes& axes, Rect2D rect) = 0;
    /// Contribute to autoscale (extend the viewport).
    virtual void contributeToAutoscale(Viewport& v) const = 0;
    /// Legend label.
    [[nodiscard]] virtual std::string label() const { return {}; }
};

/// A Figure holds one or more Axes arranged in a grid.
class Figure {
public:
    Figure() = default;
    explicit Figure(uint32_t rows, uint32_t cols);

    /// Add an Axes at grid position (row, col), spanning rowSpan×colSpan.
    Axes* addAxes(uint32_t row = 0, uint32_t col = 0,
                  uint32_t rowSpan = 1, uint32_t colSpan = 1);

    /// Layout all axes within the given pixel extent.
    void layout(Extent2D extent);

    [[nodiscard]] const std::vector<AxesPlacement>& placements() const noexcept { return placements_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }

    void setTitle(std::string t) { style_.title.text = std::move(t); }

    struct AxesPlacement { std::unique_ptr<Axes> axes; uint32_t r, c, rs, cs; };

private:
    uint32_t rows_ = 1, cols_ = 1;
    FigureStyle style_;
    std::vector<AxesPlacement> placements_;
};

} // namespace volcano::plot
