// volcano/plot/Axes.hpp — an Axes (subplot) holding plot layers
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"

#include <memory>
#include <vector>

namespace volcano::render::primitives { class ReduceRenderer; }

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
    [[nodiscard]] bool manualViewport() const noexcept { return manualViewport_; }

    /// Enable log scale on an axis.
    void setLogX(bool v) { logX_ = v; }
    void setLogY(bool v) { logY_ = v; }
    [[nodiscard]] bool logX() const noexcept { return logX_; }
    [[nodiscard]] bool logY() const noexcept { return logY_; }

    /// Convenience: log scale on both axes (matplotlib `loglog`).
    void loglog() { logX_ = true; logY_ = true; }
    /// Convenience: log scale on x only (matplotlib `semilogx`).
    void semilogx() { logX_ = true; logY_ = false; }
    /// Convenience: log scale on y only (matplotlib `semilogy`).
    void semilogy() { logX_ = false; logY_ = true; }

    void setTitle(std::string t) { style_.title.text = std::move(t); }
    void setStyle(FigureStyle s) { style_ = std::move(s); }
    [[nodiscard]] const FigureStyle& style() const noexcept { return style_; }
    [[nodiscard]] FigureStyle& style() noexcept { return style_; }

    /// Add a plot layer (scatter, line, bar, ...). Returns a raw pointer for further configuration.
    IPlot* addPlot(std::unique_ptr<IPlot> plot);

    [[nodiscard]] const std::vector<std::unique_ptr<IPlot>>& plots() const noexcept { return plots_; }

    /// Compute the data viewport from all layers if not manually set (CPU).
    void autoscale();

    /// Compute the data viewport using a GPU parallel min/max reduce over
    /// each layer's uploaded buffers. Falls back to CPU per-layer when a
    /// layer has no GPU buffer. No-op if the viewport was set manually.
    /// Must be called after each layer's `prepare()` has uploaded GPU data.
    void autoscaleGpu(render::primitives::ReduceRenderer& reducer);

    /// Pixel rect within the figure (set by Figure layout).
    Rect2D rect{};

private:
    /// Apply 5% padding and degenerate-range fixup to a raw min/max viewport.
    static void finalizeAutoscale(Viewport& v);

    Viewport viewport_{0,1,0,1};
    bool manualViewport_ = false;
    bool logX_ = false;
    bool logY_ = false;
    FigureStyle style_;
    std::vector<std::unique_ptr<IPlot>> plots_;
};

} // namespace volcano::plot
