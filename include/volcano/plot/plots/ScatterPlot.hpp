// volcano/plot/plots/ScatterPlot.hpp — scatter plot layer
#pragma once

#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
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

    /// mpl PathCollection::set_offsets — replace the point data in place.
    /// The GPU buffer is reused (memcpy) when the new point count fits
    /// the existing allocation; the owning axes is marked stale.
    void setData(std::vector<float> x, std::vector<float> y);
    void setOffsets(std::vector<Point2D> points);

    // ── mpl ScalarMappable (scatter c= scalar array) ────────────────
    /// Scalar array colormapped through `cmap_` + `norm_` — one value
    /// per point (mpl scatter c=<array>). When empty, all points draw
    /// in series_.color.
    std::vector<float> array_;
    /// Per-point explicit RGBA colors (mpl c=<list of colors>) —
    /// bypasses norm/cmap entirely. Mutually exclusive with array_.
    std::vector<Color> colors_;
    /// Per-point marker diameters in px (mpl scatter s=<array>).
    /// Empty → uniform series_.size.
    std::vector<float> sizes_;
    /// Norm mapping array_ → [0,1] for cmap sampling (mpl norm=;
    /// default linear, autoscaled from array_). Lazily created by
    /// ensureNorm() — mutable so const autoscale paths can fill it.
    mutable std::shared_ptr<Normalize> norm_;
    /// Colormap for array_ (mpl cmap=; default viridis).
    const Colormap* cmap_ = &colormaps::viridis();

    [[nodiscard]] std::shared_ptr<Normalize> norm() const override {
        return norm_;
    }
    void setNorm(std::shared_ptr<Normalize> n) override {
        norm_ = std::move(n);
        touch();
    }
    [[nodiscard]] const Colormap* cmap() const override { return cmap_; }
    void setCmap(const Colormap& cm) override { cmap_ = &cm; touch(); }
    /// mpl `set_array` — replace the scalar array.
    void setArray(std::vector<float> a) override {
        array_ = std::move(a);
        touch();
    }
    [[nodiscard]] std::vector<float> array() const override {
        return array_;
    }
    /// mpl `set_clim` — explicit norm bounds (nullopt → autoscale).
    void setClim(std::optional<float> vmin,
                 std::optional<float> vmax) override;
    /// Effective scalar range: norm bounds when set, else the array's
    /// data range (autoscaled at first use). Empty without array_.
    [[nodiscard]] std::optional<Range> valueRange() const override;

    /// mpl PathCollection.set_sizes / get_sizes (marker diameters, px).
    void setSizes(std::vector<float> s) { sizes_ = std::move(s); touch(); }
    [[nodiscard]] const std::vector<float>& sizes() const { return sizes_; }

    Series2D& series() noexcept { return series_; }
    [[nodiscard]] const Series2D& series() const noexcept { return series_; }
    [[nodiscard]] bool canEmitVector() const override { return true; }
    void emitVector(render::VectorCanvas& c, const Axes& axes,
                    Rect2D rect) override;

private:
    /// Lazily create the default linear norm (mpl ScalarMappable always
    /// carries a Normalize — created on first colormapping use).
    void ensureNorm() const {
        if (!norm_) norm_ = std::make_shared<NormalizeLinear>();
    }
    /// Per-point colors for colormapped/explicit-c scatter; empty when
    /// the series draws uniformly.
    [[nodiscard]] std::vector<Color> pointColors() const;
    /// Per-point marker diameters (sizes_ padded with series_.size).
    [[nodiscard]] std::vector<float> pointSizes() const;

    Series2D series_;
    render::primitives::PointRenderer renderer_;
    std::vector<Color> pcCache_;    ///< pointColors() scratch (draw path)
    std::vector<float> psCache_;    ///< pointSizes() scratch
    bool prepared_ = false;
    bool dataDirty_ = false;
};

} // namespace volcano::plot
