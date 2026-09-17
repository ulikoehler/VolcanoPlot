// volcano/plot/plots/LinePlot.hpp — line plot layer
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include <algorithm>
namespace volcano::plot {
class LinePlot : public IPlot {
public:
    explicit LinePlot(Series2D series) : series_(std::move(series)) {}
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    void contributeToAutoscaleGpu(render::primitives::ReduceRenderer& reducer,
                                  Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return series_.label; }
    [[nodiscard]] Color legendColor() const override { return series_.color; }

    bool applyCycleProps(const CycleProps& p) override {
        if (!series_.usePropCycle) return false;
        if (p.color) series_.color = *p.color;
        if (p.lineStyle) series_.lineStyle = *p.lineStyle;
        if (p.lineWidth) series_.lineWidth = *p.lineWidth;
        if (p.marker) series_.marker = *p.marker;
        return true;
    }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

    /// Picking: hit when the data point is within half the line width
    /// (+2px) of a segment in pixel space.
    bool contains(const Axes& axes, Point2D pt) const override {
        auto px = [&](Point2D p) {
            auto f = axes.dataToFraction(p);
            return Point2D{axes.rect.x + f.x * float(axes.rect.width),
                           axes.rect.y + (1.0f - f.y) * float(axes.rect.height)};
        };
        auto q = px(pt);
        float tol = series_.lineWidth * 0.5f + 2.0f;
        for (size_t i = 0; i + 1 < series_.points.size(); ++i) {
            auto a = px(series_.points[i]);
            auto b = px(series_.points[i + 1]);
            float vx = b.x - a.x, vy = b.y - a.y;
            float len2 = vx * vx + vy * vy;
            float t = len2 > 0 ? std::clamp(((q.x - a.x) * vx + (q.y - a.y) * vy) / len2, 0.0f, 1.0f) : 0.0f;
            float dx = q.x - (a.x + t * vx), dy = q.y - (a.y + t * vy);
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
    render::primitives::LineRenderer renderer_;
    bool prepared_ = false;
};
} // namespace volcano::plot
