// volcano/plot/Axes.hpp — an Axes (subplot) holding plot layers
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Style.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Annotation.hpp"

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

    /// Add a text annotation at (x, y) in the given coordinate system.
    /// Returns a pointer to the annotation for further customization.
    TextAnnotation* text(float x, float y, std::string txt,
                         CoordSystem coords = CoordSystem::Data) {
        TextAnnotation t;
        t.x = x; t.y = y; t.coords = coords;
        t.text = std::move(txt);
        texts_.push_back(std::move(t));
        return &texts_.back();
    }

    /// Add an annotation with an arrow from xyText to xy.
    /// Returns a pointer to the annotation for further customization.
    Annotation* annotate(float xyX, float xyY, float xyTextX, float xyTextY,
                         std::string txt,
                         CoordSystem coords = CoordSystem::Data) {
        Annotation a;
        a.xy[0] = xyX; a.xy[1] = xyY;
        a.xyCoords = coords;
        a.xyText[0] = xyTextX; a.xyText[1] = xyTextY;
        a.xyTextCoords = coords;
        a.text = std::move(txt);
        annotations_.push_back(std::move(a));
        return &annotations_.back();
    }

    [[nodiscard]] const std::vector<TextAnnotation>& texts() const noexcept { return texts_; }
    [[nodiscard]] const std::vector<Annotation>& annotations() const noexcept { return annotations_; }

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
    std::vector<TextAnnotation> texts_;
    std::vector<Annotation> annotations_;
};

} // namespace volcano::plot
