// volcano/plot/plots/LinePlot.hpp — line plot layer
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/render/primitives/LineRenderer.hpp"
#include "volcano/render/primitives/GpuLineRenderer.hpp"
#include <algorithm>
namespace volcano::plot {
class LinePlot : public IPlot {
public:
    explicit LinePlot(Series2D series) : series_(std::move(series)) {
        zorder = 2.0f;  // mpl Line2D default zorder
    }
    void prepare(render::Renderer& r) override;
    /// GPU pre-pass: solid (non-dashed) lines are tessellated on the GPU
    /// into a vertex soup consumed by draw() in the same frame.
    void preDraw(vk::CommandBuffer cmd, render::Renderer& r,
                 const Axes& axes, Rect2D rect) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
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
        // Only an "auto" color consumes the cycle (matplotlib: an
        // explicit color= does not advance the prop cycle).
        bool consumed = false;
        if (series_.hasAutoColor()) {
            if (p.color) series_.color = *p.color;
            consumed = true;
        }
        if (p.lineStyle) series_.lineStyle = *p.lineStyle;
        if (p.lineWidth) series_.lineWidth = *p.lineWidth;
        if (p.marker) series_.marker = *p.marker;
        if (p.markerTex) series_.markerTex = *p.markerTex;
        return consumed;
    }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }
    /// mpl Line2D legend handle: the segment gets `numpoints` markers
    /// only when the line actually has markers (marker=None → bare line).
    [[nodiscard]] std::vector<LegendHandle> legendEntries() const override {
        if (label().empty()) return {};
        LegendHandle h{label(), legendColor(), LegendMarker::Line};
        h.points = (series_.marker != MarkerStyle::None ||
                    series_.markerPath || !series_.markerTex.empty())
                       ? -2 : 0;
        return {h};
    }

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

    /// mpl Line2D::set_data / set_xdata / set_ydata — replace the point
    /// data in place. The GPU buffer is reused (memcpy) when the new
    /// point count fits the existing allocation, and the owning axes is
    /// marked stale so renderIfStale picks the change up.
    void setData(std::vector<float> x, std::vector<float> y);
    void setXdata(std::vector<float> x);
    void setYdata(std::vector<float> y);
    /// GPU point buffer — stable handle lets tests verify in-place reuse.
    [[nodiscard]] vk::Buffer pointBuffer() const noexcept {
        return renderer_.pointBuffer();
    }

    Series2D& series() noexcept { return series_; }
    [[nodiscard]] const Series2D& series() const noexcept { return series_; }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;
private:
    /// Per-pass marker overrides for patheffects (offset + recolor).
    struct MarkerFx {
        Point2D offset{0.0f, 0.0f};
        std::optional<Color> face;
        std::optional<Color> edge;
        float edgeWidth = -1.0f;  // <0 → series_.markerEdgeWidth
    };
    /// Raster marker pass (series_.marker / markerPath / markerTex).
    void drawMarkersAtPoints(vk::CommandBuffer cmd, render::Renderer& r,
                             const Axes& axes, Rect2D rect,
                             const MarkerFx* fx = nullptr);
    Series2D series_;
    render::primitives::LineRenderer renderer_;
    /// GPU-tessellated stroke produced by preDraw (valid for frameSeq()).
    render::primitives::GpuLineRenderer::Mesh gpuMesh_;
    uint64_t gpuMeshSeq_ = 0;
    bool prepared_ = false;
    bool dataDirty_ = false;
};
} // namespace volcano::plot
