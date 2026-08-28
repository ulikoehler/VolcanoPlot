// volcano/plot/Axes.hpp — an Axes (subplot) holding plot layers
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"

#include <memory>
#include <vector>

namespace volcano::plot {

class IPlot;

/// An Axes is one subplot with its own coordinate system, axes, and layers.
class Axes {
public:
    Axes() = default;

    /// Set the data viewport (axis limits).
    void setViewport(Viewport v) { viewport_ = v; manualViewport_ = true; }
    [[nodiscard]] const Viewport& viewport() const noexcept { return viewport_; }
    [[nodiscard]] Viewport& viewport() noexcept { return viewport_; }

    /// Enable log scale on an axis.
    void setLogX(bool v) { logX_ = v; }
    void setLogY(bool v) { logY_ = v; }
    [[nodiscard]] bool logX() const noexcept { return logX_; }
    [[nodiscard]] bool logY() const noexcept { return logY_; }

    void setTitle(std::string t) { style_.title.text = std::move(t); }
    void setStyle(FigureStyle s) { style_ = std::move(s); }
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }

    /// Add a plot layer (scatter, line, bar, ...). Returns a raw pointer for further configuration.
    IPlot* addPlot(std::unique_ptr<IPlot> plot);

    [[nodiscard]] const std::vector<std::unique_ptr<IPlot>>& plots() const noexcept { return plots_; }

    /// Compute the data viewport from all layers if not manually set.
    void autoscale();

    /// Pixel rect within the figure (set by Figure layout).
    Rect2D rect{};

private:
    Viewport viewport_{0,1,0,1};
    bool manualViewport_ = false;
    bool logX_ = false;
    bool logY_ = false;
    FigureStyle style_;
    std::vector<std::unique_ptr<IPlot>> plots_;
};

} // namespace volcano::plot
