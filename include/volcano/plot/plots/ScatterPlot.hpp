// volcano/plot/plots/ScatterPlot.hpp — scatter plot layer
#pragma once

#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/PointRenderer.hpp"

namespace volcano::plot {

class ScatterPlot : public IPlot {
public:
    explicit ScatterPlot(Series2D series) : series_(std::move(series)) {
        // matplotlib scatter defaults to 'o' markers.
        if (series_.marker == MarkerStyle::None && !series_.markerPath &&
            series_.markerTex.empty())
            series_.marker = MarkerStyle::Circle;
    }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// Log/logit scales drop out-of-domain points from the data limits.
    void contributeToAutoscaleScaled(Viewport& v, const AxisScale& xscale,
                                     const AxisScale& yscale) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return series_.label; }
    [[nodiscard]] Color legendColor() const override {
        return series_.resolvedColor();
    }

    bool applyCycleProps(const CycleProps& p) override {
        if (!series_.usePropCycle) return false;
        bool consumed = false;
        if (series_.hasAutoColor()) {
            if (p.color) series_.color = *p.color;
            consumed = true;
        }
        if (p.marker) series_.marker = *p.marker;
        if (p.markerTex) series_.markerTex = *p.markerTex;
        if (p.lineWidth) series_.lineWidth = *p.lineWidth;
        if (p.lineStyle) series_.lineStyle = *p.lineStyle;
        return consumed;
    }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Circle; }

    /// Fraction of series points inside the data-space box (loc="best").
    [[nodiscard]] float occupancy(Range xr, Range yr) const override {
        if (series_.points.empty()) return 0.0f;
        size_t n = 0;
        for (const auto& p : series_.points)
            if (p.x >= xr.min && p.x <= xr.max &&
                p.y >= yr.min && p.y <= yr.max)
                ++n;
        return float(n) / float(series_.points.size());
    }

    /// Picking: hit when the data point lands within a marker's radius
    /// (+2px tolerance) in pixel space.
    bool contains(const Axes& axes, Point2D pt) const override {
        auto px = [&](Point2D p) {
            auto f = axes.dataToFraction(p);
            return Point2D{axes.rect.x + f.x * float(axes.rect.width),
                           axes.rect.y + (1.0f - f.y) * float(axes.rect.height)};
        };
        auto q = px(pt);
        float tol = series_.size * 0.5f + 2.0f;
        for (const auto& p : series_.points) {
            auto c = px(p);
            float dx = q.x - c.x, dy = q.y - c.y;
            if (dx * dx + dy * dy <= tol * tol) return true;
        }
        return false;
    }

    Series2D& series() noexcept { return series_; }
    [[nodiscard]] const Series2D& series() const noexcept { return series_; }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

private:
    Series2D series_;
    render::primitives::PointRenderer renderer_;
    bool prepared_ = false;
};

} // namespace volcano::plot
