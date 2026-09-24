// volcano/plot/plots/HeatmapPlot.hpp
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/plot/Normalize.hpp"
#include "volcano/render/primitives/HeatmapRenderer.hpp"
namespace volcano::plot {
class HeatmapPlot : public IPlot {
public:
    HeatmapPlot(Grid2D grid, const Colormap& cmap = colormaps::viridis())
        : grid_(std::move(grid)), cmap_(&cmap) {
        zorder = 0.0f;  // mpl AxesImage default zorder
    }
    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    /// mpl sticky edges: tight autoscale, no 5% margin.
    [[nodiscard]] bool tightAutoscale() const override { return true; }

    // ── mpl ScalarMappable (AxesImage) ───────────────────────────────
    /// mpl `norm=` — when set, grid values map through it before the
    /// colormap LUT is sampled (CPU-side pre-transform; the shader then
    /// interpolates linearly over the normalized range).
    [[nodiscard]] std::shared_ptr<Normalize> norm() const override {
        return norm_;
    }
    void setNorm(std::shared_ptr<Normalize> n) override {
        norm_ = std::move(n);
        dirty_ = true;
        touch();
    }
    [[nodiscard]] const Colormap* cmap() const override { return cmap_; }
    void setCmap(const Colormap& cm) override {
        cmap_ = &cm;
        dirty_ = true;
        touch();
    }
    /// mpl `set_array` — replace the scalar field (must keep w×h).
    void setArray(std::vector<float> a) override;
    /// mpl `set_array` with reshape — AxesImage accepts any shape and
    /// the image extent follows (xRange/yRange become -0.5..n-0.5).
    void setArrayReshaped(std::vector<float> a, uint32_t w, uint32_t h);
    /// mpl `get_array` — the flat scalar field (row-major w×h).
    [[nodiscard]] std::vector<float> array() const override {
        return grid_.values;
    }
    /// mpl `set_clim` — explicit norm bounds (nullopt → autoscale).
    void setClim(std::optional<float> vmin,
                 std::optional<float> vmax) override;
    /// The scalar range (drives the colorbar): the norm's bounds when
    /// a norm is in play, else the data/valueRange.
    [[nodiscard]] std::optional<Range> valueRange() const override;
    /// mpl `im.alpha` — uniform opacity multiplier.
    void setAlpha(float a) { alpha_ = a; dirty_ = true; touch(); }
    [[nodiscard]] float alpha() const { return alpha_; }
    /// Grid dimensions (for bindings that reshape the flat array).
    [[nodiscard]] std::pair<uint32_t, uint32_t> dims() const {
        return {grid_.width, grid_.height};
    }
    /// mpl AxesImage.get_extent — the data-space image bounds.
    [[nodiscard]] const Grid2D& grid() const { return grid_; }
    [[nodiscard]] Grid2D& mutableGrid() { return grid_; }
    /// mpl AxesImage.set_extent — set the data-space extent verbatim
    /// (unsorted; a reversed range yields an inverted axis, matching
    /// mpl's `extent` ↔ `origin` interplay).
    void setExtent(float x0, float x1, float y0, float y1) {
        grid_.xRange = {x0, x1};
        grid_.yRange = {y0, y1};
        dirty_ = true;
        touch();
    }
    /// mpl RGB(A) image data (empty for scalar images).
    [[nodiscard]] const std::vector<uint32_t>& rgba() const {
        return grid_.rgba;
    }
    /// mpl AxesImage.set_array on RGB data — replace the pixel buffer.
    void setRgba(std::vector<uint32_t> px, uint32_t w, uint32_t h);

private:
    /// Ensure a norm exists (mpl AxesImage always has one — default
    /// linear) so set_clim works even without an explicit norm=.
    void ensureNorm() const {
        if (!norm_) norm_ = std::make_shared<NormalizeLinear>();
    }
    /// The upload grid: norm-transformed values + a {0,1} range when a
    /// norm is set, else the raw grid with its data value range.
    [[nodiscard]] Grid2D effectiveGrid() const;

    Grid2D grid_;
    const Colormap* cmap_;
    mutable std::shared_ptr<Normalize> norm_;
    float alpha_ = 1.0f;
    render::primitives::HeatmapRenderer renderer_;
    bool prepared_ = false;
    /// norm/cmap/array changed → re-upload the value texture.
    bool dirty_ = false;
};
} // namespace volcano::plot
