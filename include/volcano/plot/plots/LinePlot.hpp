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
    void preDraw(render::Cmd& cmd, render::Renderer& r,
                 const Axes& axes, Rect2D rect) override;
    void draw(render::Cmd& cmd, render::Renderer& r, const Axes& axes, Rect2D rect) override;
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

    void applyStyleDefaults(const FigureStyle& s) override {
        // mpl: rcParams['lines.linewidth'] (points) is the Line2D
        // default; Series2D::lineWidth is in pixels → scale by dpi/72.
        if (series_.lineWidth <= 0.0f)
            series_.lineWidth = s.lines.lineWidth * s.dpi / 72.0f;
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
        // Cycle linewidth only fills an auto width — an explicit lw= wins
        // (mpl: cycle props are defaults for keys not passed explicitly).
        if (p.lineWidth && series_.lineWidth <= 0.0f)
            series_.lineWidth = *p.lineWidth;
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
        ensureCpuPoints();
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
        ensureCpuPoints();
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
    /// Interleaved-f64 variant: the raw dvec2 data is canonical and
    /// uploads to the device unchanged (plain memcpy) — the f64→f32
    /// narrowing runs on the GPU (GpuLineRenderer::convertF64ToF32)
    /// instead of an O(N) host conversion. series_.points is then only
    /// materialized lazily by CPU consumers via ensureCpuPoints().
    void setDataF64(std::vector<double> xy);
    /// Span variant — assigns into the existing rawF64_ allocation so
    /// steady-state updates don't malloc a fresh buffer per call.
    void setDataF64(std::span<const double> xy);
    /// Move the raw interleaved f64 back out (used when the owning FFI
    /// figure rebuilds its plots from trace state).
    [[nodiscard]] std::vector<double> takeRawF64();
    /// Sliding-window update for live traces: `drop` points are
    /// evicted from the front and `tailXY` (interleaved f64) appended
    /// at the back. rawF64_ keeps a logical window [rawFront_, end) so
    /// the host never memmoves; the GPU side receives only the tail
    /// via a ring-buffered buffer (updatePointsF64Append). Callers
    /// must guarantee drop <= dataCount() and that the retained suffix
    /// is unchanged — anything else needs a full setDataF64.
    void slideDataF64(uint32_t drop, std::span<const double> tailXY);
    /// Point count regardless of which store is canonical.
    [[nodiscard]] size_t dataCount() const noexcept {
        return useF64_ ? rawF64_.size() / 2 - rawFront_
                       : series_.points.size();
    }
    /// GPU point buffer — stable handle lets tests verify in-place reuse.
    [[nodiscard]] render::GpuBuf pointBuffer() const noexcept {
        return renderer_->pointBuffer();
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
    void drawMarkersAtPoints(render::Cmd& cmd, render::Renderer& r,
                             const Axes& axes, Rect2D rect,
                             const MarkerFx* fx = nullptr);
    /// Materialize series_.points from rawF64_ — called by every CPU
    /// path that reads point values (markers, fallbacks, autoscale,
    /// picking). No-op when the f32 store is canonical or already
    /// materialized. const so const consumers can call it.
    void ensureCpuPoints() const;
    Series2D series_;
    std::unique_ptr<render::primitives::LineRenderer> renderer_;
    /// GPU-tessellated stroke produced by preDraw (valid for frameSeq()).
    std::vector<render::primitives::GpuLineRenderer::Mesh> gpuMeshes_;
    uint64_t gpuMeshSeq_ = 0;
    /// Persistent mesh store + input cache for the device transform
    /// path: while data/transform/rect/stroke are unchanged the plot
    /// reuses the previous frame's tessellation instead of recording
    /// transform + tessellate dispatches again — the dominant per-frame
    /// cost on multi-trace live plots where only some traces churn.
    std::unique_ptr<render::primitives::GpuLineRenderer::MeshStore>
        meshStore_;
    std::vector<render::primitives::GpuLineRenderer::Mesh>
        persistentMeshes_;
    /// Everything the device path bakes into the mesh — compared
    /// memberwise each preDraw.
    struct MeshCacheKey {
        uint64_t dataVer = 0;
        uint32_t count = 0;
        float viewMinX = 0, viewSpanX = 0, viewMinY = 0, viewSpanY = 0;
        int codeX = 0, codeY = 0;
        float sxP1 = 0, sxP2 = 0, sxP3 = 0;
        float syP1 = 0, syP2 = 0, syP3 = 0;
        float rectX = 0, rectY = 0, rectW = 0, rectH = 0;
        float width = 0;
        int join = 0, cap = 0;
        float miterLimit = 0;
        float r = 0, g = 0, b = 0, a = 0;
        bool operator==(const MeshCacheKey&) const = default;
    };
    MeshCacheKey meshKey_{};
    uint64_t meshEpoch_ = 0;    ///< gpuLine().meshEpoch() at cache time
    bool meshKeyValid_ = false;
    /// Pixel translate applied when drawing the cached mesh — nonzero
    /// on a pure viewport pan (viewMin moved, everything else equal).
    Point2D meshOffset_{0, 0};
    /// Bumped by every data mutator — part of MeshCacheKey.
    uint64_t dataVersion_ = 0;
    /// Canonical f64 data while useF64_ — series_.points stays empty
    /// until ensureCpuPoints() materializes it.
    std::vector<double> rawF64_;
    /// Logical window start (points) inside rawF64_ — slideDataF64
    /// advances this instead of erasing; compacted lazily.
    size_t rawFront_ = 0;
    /// Pending incremental upload for prepare(): front-drop count and
    /// the tail's element range inside rawF64_.
    bool slidePending_ = false;
    uint32_t slideDrop_ = 0;
    size_t slideTailOff_ = 0;
    size_t slideTailLen_ = 0;
    bool prepared_ = false;
    bool dataDirty_ = false;
    bool useF64_ = false;
    mutable bool cpuDirty_ = false;   ///< rawF64_ not yet in series_.points
    bool f64Failed_ = false;          ///< device lacks the fp64 path
};
} // namespace volcano::plot
