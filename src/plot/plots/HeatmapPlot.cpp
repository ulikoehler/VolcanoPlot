// volcano/plot/plots/HeatmapPlot.cpp
#include "volcano/plot/plots/HeatmapPlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <format>
#include <limits>
namespace volcano::plot {

void HeatmapPlot::setRgba(std::vector<uint32_t> px, uint32_t w,
                          uint32_t h) {
    if (px.size() != size_t(w) * h)
        throw std::invalid_argument(
            std::format("set_array: {} pixels don't fit {}x{}",
                        px.size(), w, h));
    grid_.rgba = std::move(px);
    grid_.values.clear();
    grid_.width = w;
    grid_.height = h;
    grid_.xRange = {-0.5f, float(w) - 0.5f};
    grid_.yRange = {-0.5f, float(h) - 0.5f};
    dirty_ = true;
    touch();
}

void HeatmapPlot::setArray(std::vector<float> a) {
    if (a.size() != grid_.values.size())
        throw std::invalid_argument(
            std::format("set_array: expected {} values, got {}",
                        grid_.values.size(), a.size()));
    grid_.values = std::move(a);
    // The explicit valueRange may no longer match the data — clear it
    // so the norm/value range re-autoscales (mpl set_array behavior).
    grid_.valueRange = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::lowest()};
    dirty_ = true;
    touch();
}

void HeatmapPlot::setArrayReshaped(std::vector<float> a, uint32_t w,
                                   uint32_t h) {
    if (a.size() != size_t(w) * h)
        throw std::invalid_argument(
            std::format("set_array: {} values don't fit {}x{}", a.size(),
                        w, h));
    grid_.values = std::move(a);
    grid_.width = w;
    grid_.height = h;
    // mpl image extent: -0.5..w-0.5, -0.5..h-0.5.
    grid_.xRange = {-0.5f, float(w) - 0.5f};
    grid_.yRange = {-0.5f, float(h) - 0.5f};
    grid_.valueRange = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::lowest()};
    dirty_ = true;
    touch();
}

void HeatmapPlot::setClim(std::optional<float> vmin,
                          std::optional<float> vmax) {
    ensureNorm();
    norm_->setVmin(vmin.value_or(std::nanf("")));
    norm_->setVmax(vmax.value_or(std::nanf("")));
    dirty_ = true;
    touch();
}

Grid2D HeatmapPlot::effectiveGrid() const {
    if (!grid_.rgba.empty() || !norm_) return grid_;
    Grid2D g = grid_;
    // mpl: norm autoscales from the data when vmin/vmax are unset.
    norm_->autoscale(grid_.values);
    for (auto& v : g.values)
        if (!std::isnan(v)) v = (*norm_)(v);
    g.valueRange = {0.0f, 1.0f};
    return g;
}

std::optional<Range> HeatmapPlot::valueRange() const {
    if (!grid_.rgba.empty()) return std::nullopt;
    if (norm_) {
        ensureNorm();
        norm_->autoscale(grid_.values);
        if (norm_->hasRange())
            return Range{norm_->vmin(), norm_->vmax()};
    }
    Range vr = grid_.valueRange;
    if (!vr.valid() && !grid_.values.empty()) {
        auto [lo, hi] = std::ranges::minmax(grid_.values);
        vr = {lo, hi};
    }
    return vr.valid() ? std::optional{vr} : std::nullopt;
}

void HeatmapPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    if (!prepared_)
        renderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache(),
                       r.descriptorPool());
    if (!prepared_ || dirty_) {
        Grid2D g = effectiveGrid();
        // matplotlib auto-normalizes from data when vmin/vmax aren't
        // given (and no norm is set).
        if (!norm_ &&
            g.valueRange.min > g.valueRange.max && !g.values.empty()) {
            auto [lo, hi] = std::ranges::minmax(g.values);
            g.valueRange = {lo, hi};
        }
        // mpl alpha on RGB data: bake the multiplier into the pixels.
        if (alpha_ < 1.0f && !g.rgba.empty())
            for (auto& p : g.rgba) {
                uint32_t a = (p >> 24) & 0xFF;
                a = uint32_t(a * alpha_ + 0.5f);
                p = (p & 0x00FFFFFFu) | (a << 24);
            }
        // mpl alpha: bake the uniform multiplier into the LUT stops.
        Colormap cmScaled = *cmap_;
        if (alpha_ < 1.0f) {
            for (auto& s : cmScaled.stops) s.a *= alpha_;
            if (cmScaled.bad) cmScaled.bad->a *= alpha_;
            if (cmScaled.under) cmScaled.under->a *= alpha_;
            if (cmScaled.over) cmScaled.over->a *= alpha_;
        }
        renderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                         ctx.graphicsPool.handle(), ctx.allocator.handle(),
                         g, cmScaled);
        dirty_ = false;
    }
    prepared_ = true;
}
void HeatmapPlot::draw(vk::CommandBuffer cmd, render::Renderer& r, const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect = clipRectVk(rect, r.backend().extent());
    renderer_.draw(cmd, vrect, t);
}
void HeatmapPlot::contributeToAutoscale(Viewport& v) const {
    // mpl update_datalim(extent corners) — sorted; the stored extent is
    // verbatim so origin='upper' may carry an inverted y range.
    v.x.min = std::min({v.x.min, grid_.xRange.min, grid_.xRange.max});
    v.x.max = std::max({v.x.max, grid_.xRange.min, grid_.xRange.max});
    v.y.min = std::min({v.y.min, grid_.yRange.min, grid_.yRange.max});
    v.y.max = std::max({v.y.max, grid_.yRange.min, grid_.yRange.max});
    // Scalar range feeds the colorbar's z viewport.
    if (auto vr = valueRange()) {
        v.z.min = std::min(v.z.min, vr->min);
        v.z.max = std::max(v.z.max, vr->max);
    }
}
} // namespace volcano::plot
