// volcano/render/VectorRenderer.hpp — drives a Figure onto a
// VectorCanvas, replicating the raster renderer's layout (grid, spines,
// ticks, labels, legend, colorbar, annotations) with vector primitives.
//
// Plots emit native vector geometry via IPlot::emitVector; layers that
// can't are rasterized by the RasterFn callback and embedded as bitmaps
// (matplotlib `rasterized` fallback semantics). Contiguous z-order runs
// of non-vector layers become a single image each, preserving stacking.
#pragma once

#include "volcano/render/VectorCanvas.hpp"
#include "volcano/text/MathText.hpp"
#include "volcano/plot/Plot.hpp"

#include <functional>
#include <span>

namespace volcano::render {

class VectorRenderer {
public:
    /// Glyph metrics for layout (matches text::MeasureFn).
    using MeasureFn = text::MeasureFn;
    /// Raster fallback: render `plots` (a subset of `axes`' layers) onto
    /// a transparent RGBA buffer covering the whole figure canvas.
    /// Fills `rgba`/`w`/`h`; returns false when rasterization is
    /// unavailable (non-native layers are then skipped).
    using RasterFn = std::function<bool(const plot::Axes&,
        std::span<const plot::IPlot* const>,
        std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h)>;

    /// `measure` may be null (falls back to a monospace-ish estimate).
    /// `rasterize` may be null (non-native layers are skipped).
    VectorRenderer(MeasureFn measure, RasterFn rasterize,
                   plot::Extent2D extent)
        : measure_(std::move(measure)), rasterize_(std::move(rasterize)),
          extent_(extent) {}

    /// Emit the whole figure (must already be laid out + autoscaled).
    void render(const plot::Figure& fig, VectorCanvas& canvas);

private:
    void emitAxes(const plot::Axes& axes, plot::Rect2D rect,
                  VectorCanvas& c);
    void emitGrid(const plot::Axes& axes, plot::Rect2D rect,
                  VectorCanvas& c);
    void emitPlots(const plot::Axes& axes, plot::Rect2D rect,
                   VectorCanvas& c);
    void emitSpinesTicks(const plot::Axes& axes, plot::Rect2D rect,
                         VectorCanvas& c);
    void emitLabels(const plot::Axes& axes, plot::Rect2D rect,
                    VectorCanvas& c);
    void emitAnnotations(const plot::Axes& axes, plot::Rect2D rect,
                         VectorCanvas& c);
    void emitLegend(const plot::Axes& axes, plot::Rect2D rect,
                    VectorCanvas& c);
    void emitColorbar(const plot::Axes& axes, plot::Rect2D rect,
                      VectorCanvas& c);

    /// Text with MathText support: emits each run/rule.
    void richText(VectorCanvas& c, std::string_view text, float x, float y,
                  plot::Color color, float scale, float rotation = 0.0f,
                  plot::HAlign lineAlign = plot::HAlign::Left);
    text::TextMeasure measure(std::string_view s, float scale);
    void fillRect(VectorCanvas& c, plot::Rect2D r, plot::Color col);
    void strokeRect(VectorCanvas& c, plot::Rect2D r, const VectorCanvas::Pen&);
    void line(VectorCanvas& c, plot::Point2D a, plot::Point2D b,
              const VectorCanvas::Pen&);

    MeasureFn measure_;
    RasterFn rasterize_;
    plot::Extent2D extent_;
};

} // namespace volcano::render
