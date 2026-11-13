// volcano/plot/plots/LinePlot.cpp
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/render/Offload.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
#include "volcano/render/primitives/ReduceRenderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include "volcano/plot/Stroke.hpp"
#include "volcano/text/MathText.hpp"
#include "../MarkerDraw.hpp"
#include "../VectorEmitHelpers.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace volcano::plot {
void LinePlot::setData(std::vector<float> x, std::vector<float> y) {
    const size_t n = std::min(x.size(), y.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i)
        series_.points[i] = {x[i], y[i]};
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}
void LinePlot::setXdata(std::vector<float> x) {
    ensureCpuPoints();
    useF64_ = false;
    const size_t n = std::min(x.size(), series_.points.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i) series_.points[i].x = x[i];
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}
void LinePlot::setYdata(std::vector<float> y) {
    ensureCpuPoints();
    useF64_ = false;
    const size_t n = std::min(y.size(), series_.points.size());
    series_.points.resize(n);
    for (size_t i = 0; i < n; ++i) series_.points[i].y = y[i];
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}

void LinePlot::setDataF64(std::vector<double> xy) {
    rawF64_ = std::move(xy);
    rawFront_ = 0;
    slidePending_ = false;
    useF64_ = true;
    cpuDirty_ = true;
    series_.points.clear();
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}

void LinePlot::setDataF64(std::span<const double> xy) {
    rawF64_.assign(xy.begin(), xy.end());
    rawFront_ = 0;
    slidePending_ = false;
    useF64_ = true;
    cpuDirty_ = true;
    series_.points.clear();
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}

void LinePlot::slideDataF64(uint32_t drop,
                            std::span<const double> tailXY) {
    const size_t have = dataCount();
    if (drop > have) drop = static_cast<uint32_t>(have);
    rawFront_ += drop;
    // Compact once the dead prefix outweighs a quarter of the stored
    // range — amortized O(N), keeps rawF64_ bounded on slides.
    if (rawFront_ && rawFront_ * 8 >= rawF64_.size()) {
        if (slidePending_) slideTailOff_ -= rawFront_ * 2;
        rawF64_.erase(rawF64_.begin(),
                      rawF64_.begin() +
                          static_cast<std::ptrdiff_t>(rawFront_) * 2);
        rawFront_ = 0;
    }
    // Multiple slides can land before one prepare() — accumulate the
    // drop and keep slideTailOff_ anchored at the FIRST pending slide
    // so prepare() replays drop+tails atomically instead of only the
    // last one (which would leave a stale prefix joined to the tail).
    if (!slidePending_) {
        slideTailOff_ = rawF64_.size();
        slideDrop_ = 0;
    }
    slideDrop_ += drop;
    rawF64_.insert(rawF64_.end(), tailXY.begin(), tailXY.end());
    slideTailLen_ = rawF64_.size() - slideTailOff_;
    useF64_ = true;
    cpuDirty_ = true;
    series_.points.clear();
    slidePending_ = true;
    dataDirty_ = true;
    ++dataVersion_;
    touch();
}

std::vector<double> LinePlot::takeRawF64() {
    if (useF64_) {
        std::vector<double> v;
        v.reserve(dataCount() * 2);
        v.insert(v.end(), rawF64_.begin() +
                              static_cast<std::ptrdiff_t>(rawFront_) * 2,
                 rawF64_.end());
        rawF64_.clear();
        rawFront_ = 0;
        return v;
    }
    std::vector<double> v;
    v.reserve(series_.points.size() * 2);
    for (const auto& p : series_.points) {
        v.push_back(p.x);
        v.push_back(p.y);
    }
    return v;
}

void LinePlot::ensureCpuPoints() const {
    if (!useF64_ || !cpuDirty_) return;
    auto& pts = const_cast<LinePlot*>(this)->series_.points;
    const size_t n = dataCount();
    const double* d = rawF64_.data() + rawFront_ * 2;
    pts.resize(n);
    for (size_t i = 0; i < n; ++i)
        pts[i] = {static_cast<float>(d[2 * i]),
                  static_cast<float>(d[2 * i + 1])};
    cpuDirty_ = false;
}

void LinePlot::prepare(render::Renderer& r) {
    if (prepared_) {
        // In-place update: memcpy into the host-visible buffer (reallocs
        // only on growth). Skip when the data hasn't changed — re-uploading
        // every frame costs O(N) per trace per render, which dominates CPU
        // on live plots with several large traces. The f64 path uploads
        // the raw dvec2 data — one memcpy, no host-side narrowing.
        if (dataDirty_) {
            bool ok = false;
            if (useF64_ && !f64Failed_) {
                if (slidePending_)
                    ok = renderer_->updatePointsF64Append(
                        slideDrop_,
                        std::span<const double>{
                            rawF64_.data() + slideTailOff_,
                            slideTailLen_});
                if (!ok)
                    ok = renderer_->updatePointsF64(
                        std::span<const double>{
                            rawF64_.data() + rawFront_ * 2,
                            dataCount() * 2});
            }
            if (!ok) {
                if (useF64_) f64Failed_ = true;
                ensureCpuPoints();
                renderer_->updatePoints(std::span{series_.points});
            }
            slidePending_ = false;
            dataDirty_ = false;
        }
        return;
    }
    if (!renderer_) renderer_ = r.gpu().createLineRenderer();
    // Upload only the LIVE window [rawFront_, end) — slides landing
    // before the first prepare leave a dead prefix in rawF64_ that
    // must not reach the GPU (it would stroke stale points joined
    // to the newest data).
    if (useF64_ &&
        renderer_->updatePointsF64(std::span<const double>{
            rawF64_.data() + static_cast<std::ptrdiff_t>(rawFront_) * 2,
            dataCount() * 2})) {
        renderer_->setStyle(series_.resolvedColor(), series_.lineWidth);
    } else {
        if (useF64_) f64Failed_ = true;
        ensureCpuPoints();
        renderer_->upload(std::span{series_.points},
                          series_.resolvedColor(), series_.lineWidth);
    }
    // The full upload already reflects any pending slide — consuming
    // it again would double-drop and re-append the tail.
    slidePending_ = false;
    prepared_ = true;
    dataDirty_ = false;
}
namespace {

/// Shared pixel-space polyline computation: drawStyle expansion, scale
/// masking (NaN splits) and data→pixel transform. Used by both the GPU
/// pre-pass (preDraw) and the CPU fallback in draw(). A non-null
/// `xf` (mpl transform=) bypasses dataToFraction — it maps to display
/// pixels directly.
std::vector<Point2D> pixelPoints(const Series2D& series, const Axes& axes,
                                 Rect2D rect, const Transform* xf = nullptr) {
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series.points;
    if (!xf && (axes.xscale().clipsDomain() || axes.yscale().clipsDomain())) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }
    auto pts = applyDrawStyle(src, series.drawStyle);
    std::vector<Point2D> px;
    px.reserve(pts.size());
    for (const auto& p : pts) {
        if (xf) {
            px.push_back(xf->apply(p));
            continue;
        }
        auto f = axes.dataToFraction(p);
        px.push_back({rect.x + f.x * float(rect.width),
                      rect.y + (1.0f - f.y) * float(rect.height)});
    }
    return px;
}

/// mpl `Line2D` markevery subsampling (lines._mark_every_path): returns
/// the indices into `px` — the finite display-space marker positions —
/// that get a marker. `diag` is the axes-box diagonal in px, which mpl
/// uses to scale the fractional form (the transAxes bbox diagonal).
std::vector<size_t> markerIndices(const Series2D& series,
                                  std::span<const Point2D> px,
                                  float diag) {
    const size_t n = px.size();
    if (n == 0) return {};
    if (!series.markevery) {
        std::vector<size_t> all(n);
        std::iota(all.begin(), all.end(), size_t{0});
        return all;
    }
    std::vector<size_t> out;
    // verts[slice(start, None, step)] — python slice semantics.
    auto countForm = [&](long start, long step) {
        if (start < 0) start += long(n);
        if (step <= 0) {
            if (start >= 0 && start < long(n)) out.push_back(size_t(start));
            return;
        }
        for (long i = start; i < long(n); i += step)
            out.push_back(size_t(i));
    };
    // (start, step) float form: targets along the cumulative arc length
    // at `step` × diag spacing; the nearest actual vertex is used.
    auto fracForm = [&](double start, double step) {
        if (step <= 0 || diag <= 0) return;
        std::vector<double> delta(n);
        for (size_t i = 1; i < n; ++i)
            delta[i] = delta[i - 1] +
                std::hypot(px[i].x - px[i - 1].x, px[i].y - px[i - 1].y);
        const double scale = double(diag);
        for (double d = start * scale; d < delta.back(); d += step * scale) {
            size_t best = 0;
            double bd = std::abs(delta[0] - d);
            for (size_t i = 1; i < n; ++i) {
                double dd = std::abs(delta[i] - d);
                if (dd < bd) { bd = dd; best = i; }
            }
            // argmin is non-decreasing in d → consecutive dedup = unique.
            if (out.empty() || out.back() != best) out.push_back(best);
        }
    };
    const MarkerEvery& me = *series.markevery;
    if (const auto* v = std::get_if<int>(&me)) countForm(0, *v);
    else if (const auto* p = std::get_if<std::pair<int, int>>(&me))
        countForm(p->first, p->second);
    else if (const auto* v = std::get_if<float>(&me)) fracForm(0.0, *v);
    else if (const auto* p = std::get_if<std::pair<float, float>>(&me))
        fracForm(p->first, p->second);
    else if (const auto* idx = std::get_if<std::vector<int>>(&me)) {
        for (int i : *idx) {
            long j = i < 0 ? long(n) + i : i;
            if (j >= 0 && j < long(n)) out.push_back(size_t(j));
        }
    }
    return out;
}

} // namespace

void LinePlot::preDraw(render::Cmd& cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_ || dataCount() < 2 ||
        series_.lineStyle == LineStyle::None ||
        axes.style().sketchScale > 0.0f)
        return;
    // Fresh tessellation below bakes positions at the current view —
    // the pan-reuse path re-assigns it.
    meshOffset_ = {0, 0};
    // Sketch lines stay on the CPU stroker (they jitter per vertex).
    // Dashes now ride the GPU path too: the pattern is expanded on the
    // device from per-point cumulative arc lengths, so a dashed line no
    // longer costs a host-side stroke expansion every frame. The
    // `dashes` offload switch pins them back to the CPU when wanted.
    StrokeParams sp;
    sp.width = series_.lineWidth;
    sp.dashes = series_.dashes.empty()
                    ? dashPattern(series_.lineStyle, series_.lineWidth)
                    : series_.dashes;
    if (!sp.dashes.empty() &&
        (render::OffloadConfig::forceCpu(
             render::OffloadConfig::global().dashes) ||
         !r.gpu().gpuLine().supportsDashes())) return;
    sp.dashOffset = series_.dashOffset;
    sp.join = series_.joinStyle;
    sp.cap = series_.capStyle;
    auto& gpu = r.gpu().gpuLine();
    if (!gpu.inited()) return;
    // Point-buffer reallocs recycle VkBuffer handles — any cached
    // descriptor set referencing the old object silently binds the
    // wrong buffer (observed: envelope reduce reading f64 ring data
    // as f32, emitting NaN/garbage columns). bufEpoch is bumped on
    // every realloc, so steady-state frames keep their cached sets.
    if (renderer_->bufEpoch() != lastBufEpoch_) {
        gpu.invalidatePointSets();
        lastBufEpoch_ = renderer_->bufEpoch();
    }
    const float W = float(r.gpu().extent().width);
    const size_t huge =
        size_t(std::max(8192.0f, W * 4.0f));
    const bool envelopeOk =
        !transform && dataCount() > huge &&
        !render::OffloadConfig::forceCpu(
            render::OffloadConfig::global().envelope) &&
        series_.drawStyle == DrawStyle::Default &&
        axes.projection().kind == ProjectionKind::Rectilinear &&
        axes.xscale().kind == ScaleKind::Linear &&
        axes.yscale().kind == ScaleKind::Linear;
    const bool xformOk =
        !transform && series_.drawStyle == DrawStyle::Default &&
        !render::OffloadConfig::forceCpu(
            render::OffloadConfig::global().xform) &&
        dataCount() > huge &&
        renderer_->pointCount() == dataCount() &&
        axes.projection().kind == ProjectionKind::Rectilinear &&
        axes.xscale().shaderSupported() && axes.yscale().shaderSupported();
    // Fused f64→pixel transform: when the device transform path will run
    // on an f64 trace, transformPointsF64 reads the dvec2 buffer
    // directly — the standalone f32 narrow is only recorded when a real
    // consumer (envelope, f32 transform, CPU fallback) needs it.
    const bool fuseF64 = useF64_ && !f64Failed_ && !envelopeOk && xformOk;
    // Narrow a pending f64 upload into the f32 point buffer on the
    // device — one compute dispatch instead of an O(N) host pass.
    // Permanent fallback to the f32 upload path when the device has
    // no fp64 support.
    uint32_t ringHead = 0, ringCap = 0;
    if (useF64_) renderer_->f64Ring(ringHead, ringCap);
    auto narrow64 = [&]() {
        if (!f64Failed_ &&
            gpu.convertF64ToF32(cmd, renderer_->pointBufferF64(),
                                renderer_->pointBuffer(),
                                renderer_->pointCount(),
                                ringHead, ringCap)) {
            renderer_->clearF64Pending();
        } else {
            f64Failed_ = true;
            ensureCpuPoints();
            renderer_->updatePoints(std::span{series_.points});
            renderer_->clearF64Pending();
        }
    };
    if (useF64_ && renderer_->f64Pending() && !fuseF64)
        narrow64();
    // Massively oversampled x-monotonic polylines on linear scales:
    // decimate the *data* points into a per-pixel-column envelope before
    // any data→pixel mapping — O(n) scalar pass instead of transforming
    // all n points. Only ~2 envelope verts per column get mapped.
    if (envelopeOk) {
        const auto& vp = axes.viewport();
        float fx0 = axes.xscale().forward(vp.x.min);
        float fx1 = axes.xscale().forward(vp.x.max);
        float kx = (fx1 != fx0) ? float(rect.width) / (fx1 - fx0) : 0.0f;
        float fy0 = axes.yscale().forward(vp.y.min);
        float fy1 = axes.yscale().forward(vp.y.max);
        float ky = (fy1 != fy0) ? 1.0f / (fy1 - fy0) : 0.0f;
        float pyA = float(rect.y) + float(rect.height);
        float pyB = float(rect.height) * ky;
        // Column affine: pixel col = ax + kx * dataX  →  ax folds in the
        // viewport offset so fx0 lands at rect.x.
        const float ax = float(rect.x) - fx0 * kx;

        // Device-resident envelope→polyline: reduce, emit and
        // tessellate all record into the frame's compute stream — the
        // host never touches the points. Same mesh-cache key as the
        // transform path, but only exact hits reuse: the column grid
        // is pixel-anchored, so a pure pan regroups points and the
        // baked stroke can't be reused by translation.
        const auto& t = axes.transform();
        const auto rc = series_.resolvedColor();
        MeshCacheKey key{dataVersion_, uint32_t(dataCount()),
                         t.view.x.min, t.view.x.span(),
                         t.view.y.min, t.view.y.span(),
                         int(t.codeX()), int(t.codeY()),
                         t.scaleX.param1, t.scaleX.param2, t.scaleX.param3,
                         t.scaleY.param1, t.scaleY.param2, t.scaleY.param3,
                         float(rect.x), float(rect.y),
                         float(rect.width), float(rect.height),
                         sp.width, int(sp.join), int(sp.cap),
                         sp.miterLimit,
                         rc.r, rc.g, rc.b, rc.a};
        if (!meshStore_) meshStore_ = gpu.createMeshStore();
        if (meshStore_ && meshKeyValid_ && !persistentMeshes_.empty() &&
            meshEpoch_ == gpu.meshEpoch() && meshKey_ == key) {
            gpuMeshes_ = persistentMeshes_;
            meshOffset_ = {0, 0};
            gpuMeshSeq_ = r.frameSeq();
            return;
        }
        render::GpuBuf pxBuf = 0;
        uint32_t pxBase = 0;
        if (gpu.envelopePolyline(cmd, renderer_->pointBuffer(),
                                 renderer_->pointCount(), ax, kx,
                                 pyA, pyB, fy0, 0, int(W) - 1,
                                 pxBuf, &pxBase,
                                 /*deferBarrier=*/true)) {
            tessPending_ = PendingTess{pxBuf, pxBase,
                                       2u * uint32_t(W),
                                       r.frameSeq(), bool(meshStore_),
                                       key, sp, rc};
            return;
        }
        // GPU envelope with host readback: the uploaded point buffer is
        // reduced on-device into per-pixel-column min/max — works for
        // unsorted x too (atomics), and skips the O(n) host scan.
        // Falls back to the CPU pass on failure.
        std::vector<float> mn, mx;
        bool gpuOk = gpu.envelopeColumns(renderer_->pointBuffer(), renderer_->pointCount(),
            ax, kx, 0, int(W) - 1, mn, mx);
        std::vector<Point2D> px;
        bool usedGpu = gpuOk && !mn.empty();
        if (!usedGpu) ensureCpuPoints();
        if (!usedGpu &&
            !std::is_sorted(series_.points.begin(), series_.points.end(),
                            [](const Point2D& a, const Point2D& b) {
                                return a.x < b.x;
                            }))
            goto noDecimate; // CPU path still needs sorted x
        for (auto& run : usedGpu
                 ? plot::envelopeRuns(mn, mx, 0)
                 : plot::envelopeDecimateData(
                       series_.points, ax, kx, 0, int(W) - 1)) {
            if (!px.empty()) px.push_back(
                {std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::quiet_NaN()});
            for (auto p : run)
                px.push_back({p.x, pyA - (p.y - fy0) * pyB});
        }
        gpuMeshes_ = gpu.tessellate(cmd, px, sp, series_.resolvedColor());
        gpuMeshSeq_ = r.frameSeq();
        return;
    }
    // Device-side data→pixel mapping (`xform` offload switch): the data
    // points are already resident, so the closed-form scales and the
    // log/logit domain masking run in one compute pass and the stroker
    // consumes the result directly — the host never touches the points.
    // Custom mpl transforms, sketch/steps styles and non-rectilinear
    // projections stay on the CPU pass.
    if (xformOk) {
        // Persistent-mesh cache: when every input to transform +
        // tessellate is unchanged, skip both dispatches and reuse last
        // frame's stroke — on live multi-trace figures only the churning
        // trace re-records; unchanged ones cost a key compare.
        const auto& t = axes.transform();
        const auto rc = series_.resolvedColor();
        MeshCacheKey key{dataVersion_, uint32_t(dataCount()),
                         t.view.x.min, t.view.x.span(),
                         t.view.y.min, t.view.y.span(),
                         int(t.codeX()), int(t.codeY()),
                         t.scaleX.param1, t.scaleX.param2, t.scaleX.param3,
                         t.scaleY.param1, t.scaleY.param2, t.scaleY.param3,
                         float(rect.x), float(rect.y),
                         float(rect.width), float(rect.height),
                         sp.width, int(sp.join), int(sp.cap),
                         sp.miterLimit,
                         rc.r, rc.g, rc.b, rc.a};
        if (!meshStore_) meshStore_ = gpu.createMeshStore();
        if (meshStore_ && meshKeyValid_ && !persistentMeshes_.empty() &&
            meshEpoch_ == gpu.meshEpoch()) {
            if (meshKey_ == key) {
                gpuMeshes_ = persistentMeshes_;
                meshOffset_ = {0, 0};
                gpuMeshSeq_ = r.frameSeq();
                return;
            }
            // Pure viewport pan: the mesh is baked for ALL points (no
            // view clipping), so a viewMin shift is exactly a rigid
            // pixel translate — reuse the stroke with a vertex offset.
            MeshCacheKey pan = meshKey_;
            pan.viewMinX = key.viewMinX;
            pan.viewMinY = key.viewMinY;
            if (pan == key) {
                gpuMeshes_ = persistentMeshes_;
                meshOffset_ = {
                    (meshKey_.viewMinX - key.viewMinX) /
                        key.viewSpanX * key.rectW,
                    (key.viewMinY - meshKey_.viewMinY) /
                        key.viewSpanY * key.rectH};
                gpuMeshSeq_ = r.frameSeq();
                return;
            }
        }
        render::GpuBuf pxBuf = 0;
        uint32_t pxBase = 0;
        bool mapped = false;
        if (useF64_ && !f64Failed_) {
            mapped = gpu.transformPointsF64(
                cmd, renderer_->pointBufferF64(), renderer_->pointCount(),
                ringHead, ringCap,
                axes.transform(), rect, pxBuf, &pxBase,
                /*deferBarrier=*/true);
            // Fused variant unavailable on this backend — narrow into
            // the f32 buffer, then take the regular f32 transform.
            if (!mapped && renderer_->f64Pending())
                narrow64();
        }
        if (!mapped)
            mapped = gpu.transformPoints(cmd, renderer_->pointBuffer(),
                                         renderer_->pointCount(),
                                         axes.transform(), rect, pxBuf,
                                         &pxBase,
                                         /*deferBarrier=*/true);
        if (mapped) {
            // Tessellation runs in preDraw2 after the frame's shared
            // transform barrier — the per-trace RAW barrier is dropped.
            tessPending_ = PendingTess{pxBuf, pxBase,
                                       renderer_->pointCount(),
                                       r.frameSeq(), bool(meshStore_),
                                       key, sp, rc};
            return;
        }
        meshKeyValid_ = false;
    } else {
        meshKeyValid_ = false;
    }
noDecimate:
    ensureCpuPoints();
    auto px = pixelPoints(series_, axes, rect, transform.get());
    // Same envelope trick post-transform for the general case (custom
    // mpl transforms, nonlinear scales, steps draw styles).
    if (px.size() > huge &&
        std::is_sorted(px.begin(), px.end(),
                       [](const Point2D& a, const Point2D& b) {
                           return a.x < b.x;
                       })) {
        std::vector<Point2D> env;
        for (auto& run : plot::columnDecimate(px, 0, int(W) - 1)) {
            if (!env.empty()) env.push_back(
                {std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::quiet_NaN()});
            env.insert(env.end(), run.begin(), run.end());
        }
        px = std::move(env);
    }
    gpuMeshes_ = gpu.tessellate(cmd, px, sp, series_.resolvedColor());
    gpuMeshSeq_ = r.frameSeq();
}

void LinePlot::preDraw2(render::Cmd& cmd, render::Renderer& r,
                        const Axes& axes, Rect2D rect) {
    (void)axes; (void)rect;
    if (!tessPending_) return;
    auto pt = std::move(*tessPending_);
    tessPending_.reset();
    // A stale entry (preDraw2 skipped by a draw subset) is dropped —
    // its buffers belong to an older arena generation.
    if (pt.seq != r.frameSeq()) return;
    auto& gpu = r.gpu().gpuLine();
    if (pt.persistent && meshStore_) {
        persistentMeshes_ = gpu.tessellateDeviceTo(
            cmd, pt.pxBuf, pt.count, pt.pxBase, *meshStore_, pt.sp,
            pt.color);
        if (!persistentMeshes_.empty()) {
            gpuMeshes_ = persistentMeshes_;
            gpuMeshSeq_ = r.frameSeq();
            meshKey_ = pt.key;
            meshEpoch_ = gpu.meshEpoch();
            meshKeyValid_ = true;
            return;
        }
    }
    gpuMeshes_ = gpu.tessellateDevice(cmd, pt.pxBuf, pt.count,
                                      pt.pxBase, pt.sp, pt.color);
    gpuMeshSeq_ = r.frameSeq();
    meshKeyValid_ = false;
}

void LinePlot::draw(render::Cmd& cmd, render::Renderer& r,
                    const Axes& axes, Rect2D rect) {
    if (!prepared_ || dataCount() == 0) return;
    const float dpi = axes.style().dpi;
    if (dataCount() < 2 ||
        series_.lineStyle == LineStyle::None) {
        // Marker-only (linestyle 'None') still draws markers.
        if (pathEffects.empty()) {
            drawMarkersAtPoints(cmd, r, axes, rect);
            return;
        }
        for (const auto& fx : pathEffects) {
            MarkerFx mfx;
            Point2D off = fx.offsetPx(dpi);
            mfx.offset = off;
            switch (fx.kind) {
            case PathEffect::Kind::Stroke:
                mfx.edge = fx.foreground.value_or(series_.resolvedColor());
                mfx.edgeWidth = fx.strokeWidthPx(
                    std::max(series_.markerEdgeWidth, 0.5f), dpi);
                break;
            case PathEffect::Kind::LineShadow:
            case PathEffect::Kind::PatchShadow: {
                Color sc = fx.shadowFor(series_.resolvedColor());
                mfx.face = sc; mfx.edge = sc;
                break;
            }
            case PathEffect::Kind::Normal:
                drawMarkersAtPoints(cmd, r, axes, rect);
                break;
            }
            if (fx.kind != PathEffect::Kind::Normal)
                drawMarkersAtPoints(cmd, r, axes, rect, &mfx);
            if (fx.thenNormal)
                drawMarkersAtPoints(cmd, r, axes, rect);
        }
        return;
    }

    // Lazily mapped pixel points — massively oversampled lines drawn
    // from the GPU-tessellated envelope mesh skip the n-point map.
    std::optional<std::vector<Point2D>> pxHold;
    auto px = [&]() -> std::vector<Point2D>& {
        if (!pxHold) {
            ensureCpuPoints();
            pxHold = pixelPoints(series_, axes, rect, transform.get());
            // xkcd-style sketch wobble (path.sketch).
            if (axes.style().sketchScale > 0.0f)
                *pxHold = sketchPolyline(*pxHold,
                                         axes.style().sketchScale * 2.0f,
                                         axes.style().sketchLength);
        }
        return *pxHold;
    };

    StrokeParams sp;
    sp.width = series_.lineWidth;
    sp.dashes = series_.dashes.empty()
                    ? dashPattern(series_.lineStyle, series_.lineWidth)
                    : series_.dashes;
    sp.dashOffset = series_.dashOffset;
    sp.join = series_.joinStyle;
    sp.cap = series_.capStyle;

    // mpl clip_on=False → clip to the whole canvas, not the axes rect.
    auto eff = clipRect(rect, r.gpu().extent());
    Rect2D clip{eff.x, eff.y, eff.width, eff.height};
    Extent2D res = r.gpu().extent();
    auto& spine = r.gpu().spine();

    // Solid lines were tessellated on the GPU in preDraw; dashes and
    // patheffects passes fall back to the CPU stroker.
    auto linePass = [&](const StrokeParams& ps, Color color,
                        Point2D off) {
        std::vector<Point2D> pts;
        std::span<const Point2D> src = px();
        if (off.x != 0.0f || off.y != 0.0f) {
            pts.reserve(px().size());
            for (auto p : px()) pts.push_back({p.x + off.x, p.y + off.y});
            src = pts;
        }
        auto mesh = strokePolyline(src, ps);
        spine.drawTriangles(cmd, clip, res, mesh.verts, color);
    };
    auto normalLine = [&] {
        // gapcolor: solid underlay first, dashed line on top.
        if (series_.gapColor.a > 0.0f && !sp.dashes.empty()) {
            StrokeParams solid = sp;
            solid.dashes.clear();
            linePass(solid, series_.gapColor, {});
        }
        if (gpuMeshSeq_ == r.frameSeq() && !gpuMeshes_.empty()) {
            for (auto& m : gpuMeshes_)
                spine.drawTrianglesGpu(cmd, clip, res, m.buffer,
                                       m.firstVertex * 6 * sizeof(float),
                                       m.vertexCount, meshOffset_);
        } else {
            linePass(sp, series_.resolvedColor(), {});
        }
    };

    if (pathEffects.empty()) {
        normalLine();
        // Markers at each vertex (matplotlib plot marker=...).
        drawMarkersAtPoints(cmd, r, axes, rect);
        return;
    }

    // mpl patheffects: each entry produces its under-pass in list order;
    // Normal / thenNormal run the plain draw at that position.
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        MarkerFx mfx;
        mfx.offset = off;
        bool markerPass = true;
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            normalLine();
            drawMarkersAtPoints(cmd, r, axes, rect);
            markerPass = false;
            break;
        case PathEffect::Kind::Stroke: {
            // mpl Stroke: same path, gc overrides (linewidth/foreground).
            StrokeParams s2 = sp;
            s2.width = fx.strokeWidthPx(series_.lineWidth, dpi);
            Color fc = fx.foreground.value_or(series_.resolvedColor());
            if (fx.alpha) fc.a = *fx.alpha;
            linePass(s2, fc, off);
            mfx.edge = fc;
            mfx.edgeWidth = fx.strokeWidthPx(
                std::max(series_.markerEdgeWidth, 0.5f), dpi);
            break;
        }
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow: {
            // mpl shadows: same geometry offset, flat shadow color.
            Color sc = fx.shadowFor(series_.resolvedColor());
            linePass(sp, sc, off);
            mfx.face = sc;
            mfx.edge = sc;
            break;
        }
        }
        if (markerPass)
            drawMarkersAtPoints(cmd, r, axes, rect, &mfx);
        if (fx.thenNormal) {
            normalLine();
            drawMarkersAtPoints(cmd, r, axes, rect);
        }
    }
}

void LinePlot::drawMarkersAtPoints(render::Cmd& cmd,
                                   render::Renderer& r,
                                   const Axes& axes, Rect2D rect,
                                   const MarkerFx* fx) {
    if (series_.size <= 0 || series_.marker == MarkerStyle::None) return;
    ensureCpuPoints();
    auto eff = clipRect(rect, r.gpu().extent());
    Rect2D clip{eff.x, eff.y, eff.width, eff.height};
    Point2D off = fx ? fx->offset : Point2D{0.0f, 0.0f};
    std::vector<Point2D> px;
    px.reserve(series_.points.size());
    for (const auto& p : series_.points) {
        // Drop out-of-domain points (log/logit scales mask them).
        if (!transform &&
            (!pointInDomain(p, axes.xscale(), axes.yscale()) ||
             !std::isfinite(p.x) || !std::isfinite(p.y)))
            continue;
        Point2D q = transform ? transform->apply(p)
            : [&] {
                  auto f = axes.dataToFraction(p);
                  return Point2D{rect.x + f.x * float(rect.width),
                                 rect.y + (1.0f - f.y) * float(rect.height)};
              }();
        px.push_back({q.x + off.x, q.y + off.y});
    }
    if (series_.markevery) {
        auto idx = markerIndices(series_, px,
                                 std::hypot(float(rect.width),
                                            float(rect.height)));
        std::vector<Point2D> sub;
        sub.reserve(idx.size());
        for (size_t i : idx) sub.push_back(px[i]);
        px = std::move(sub);
    }
    const float mew = fx && fx->edgeWidth >= 0.0f
        ? fx->edgeWidth
        : std::max(series_.markerEdgeWidth, 0.5f);
    Color lineC = series_.resolvedColor();
    auto face = series_.applyAlpha(series_.markerFaceColor);
    auto edge = series_.applyAlpha(series_.markerEdgeColor);
    if (fx && fx->face) face = *fx->face;
    if (fx && fx->edge) edge = *fx->edge;
    if (series_.markerPath) {
        drawMarkersPx(r, cmd, clip, px, markerGeom(*series_.markerPath),
                      series_.size, lineC, mew, face, edge);
    } else if (!series_.markerTex.empty()) {
        drawTexMarkersPx(r, cmd, clip, px, series_.markerTex,
                         fx && fx->face ? *fx->face : lineC, series_.size);
    } else if (series_.marker != MarkerStyle::None) {
        auto g = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        // MarkerFill::None → mpl fillstyle='none' (unfilled marker).
        bool filled = series_.markerFill != MarkerFill::None;
        if (face && face->a == 0)
            filled = false;
        if (!filled) g.filled = false;
        std::optional<render::primitives::MarkerParams> sdf;
        if (g.filled && series_.markerFill == MarkerFill::Full)
            sdf = render::primitives::MarkerParams{
                static_cast<float>(series_.marker),
                static_cast<float>(series_.markerFill),
                static_cast<float>(series_.markerNumsides),
                series_.markerAngle};
        drawMarkersPx(r, cmd, clip, px, g, series_.size, lineC,
                      mew, face, edge, {}, {}, sdf);
    }
}
void LinePlot::emitVector(render::VectorCanvas& c, const Axes& axes,
                          Rect2D rect) {
    ensureCpuPoints();
    if (series_.points.empty()) return;
    const float dpi = axes.style().dpi;
    auto toPx = [&](Point2D p) {
        if (transform) return transform->apply(p);
        auto f = axes.dataToFraction(p);
        return Point2D{rect.x + f.x * float(rect.width),
                       rect.y + (1.0f - f.y) * float(rect.height)};
    };
    // Mask out-of-domain data (log/logit) — NaN points split polylines
    // and are skipped by emitMarkerAt. A custom transform bypasses the
    // data scales (mpl transform=).
    std::vector<Point2D> masked;
    std::span<const Point2D> src = series_.points;
    if (!transform &&
        (axes.xscale().clipsDomain() || axes.yscale().clipsDomain())) {
        masked = maskPointsForScales(src, axes.xscale(), axes.yscale());
        src = masked;
    }

    auto shifted = [](std::span<const Point2D> pts, Point2D off) {
        std::vector<Point2D> out;
        out.reserve(pts.size());
        for (auto p : pts) out.push_back({p.x + off.x, p.y + off.y});
        return out;
    };
    render::VectorCanvas::Pen pen;
    pen.color = series_.resolvedColor();
    pen.width = series_.lineWidth;
    pen.dashes = series_.dashes.empty()
        ? dashPattern(series_.lineStyle, series_.lineWidth)
        : series_.dashes;
    pen.dashOffset = series_.dashOffset;
    pen.join = series_.joinStyle;
    pen.cap = series_.capStyle;

    // Marker geometry shared by all passes.
    MarkerGeom mg{};
    bool drawMarkers = series_.size > 0;
    const float mew = std::max(series_.markerEdgeWidth, 0.5f);
    if (drawMarkers) {
        if (series_.markerPath) mg = markerGeom(*series_.markerPath);
        else if (series_.marker != MarkerStyle::None)
            mg = markerGeom(series_.marker, series_.markerNumsides,
                            series_.markerAngle);
        else drawMarkers = false;
    }
    bool markerFilled = series_.markerFill != MarkerFill::None;
    auto faceOv = series_.applyAlpha(series_.markerFaceColor);
    auto edgeOv = series_.applyAlpha(series_.markerEdgeColor);
    if (faceOv && faceOv->a == 0) markerFilled = false;

    auto markerPositions = [&]() -> std::vector<Point2D> {
        std::vector<Point2D> mpx;
        mpx.reserve(src.size());
        for (const auto& dp : src) {
            auto p = toPx(dp);
            if (std::isfinite(p.x) && std::isfinite(p.y))
                mpx.push_back(p);
        }
        return mpx;
    };
    auto markerPass = [&](const std::vector<Point2D>& mpx, Point2D off,
                          std::optional<Color> face,
                          std::optional<Color> edge, float ew) {
        MarkerGeom g = mg;
        if (!markerFilled && !face) g.filled = false;
        for (auto p : mpx)
            emitMarkerPx(c, {p.x + off.x, p.y + off.y}, g, series_.size,
                         pen.color, ew,
                         face ? face : faceOv,
                         edge ? edge : edgeOv);
    };
    // mpl `markevery` subsamples the marker positions.
    std::vector<Point2D> mpx;
    if (drawMarkers) {
        mpx = markerPositions();
        if (series_.markevery) {
            auto idx = markerIndices(series_, mpx,
                std::hypot(float(rect.width), float(rect.height)));
            std::vector<Point2D> sub;
            for (size_t i : idx) sub.push_back(mpx[i]);
            mpx = std::move(sub);
        }
    }

    const bool hasLine = series_.lineStyle != LineStyle::None &&
                         dataCount() >= 2;
    std::vector<Point2D> px;
    if (hasLine) {
        auto pts = applyDrawStyle(src, series_.drawStyle);
        px.reserve(pts.size());
        for (const auto& p : pts) px.push_back(toPx(p));
        if (axes.style().sketchScale > 0.0f)
            px = sketchPolyline(px, axes.style().sketchScale * 2.0f,
                                axes.style().sketchLength);
    }
    auto normalLine = [&] {
        if (!hasLine) return;
        if (series_.gapColor.a > 0.0f && !pen.dashes.empty()) {
            auto solid = pen; solid.color = series_.gapColor;
            solid.dashes.clear();
            c.polyline(px, solid);
        }
        c.polyline(px, pen);
    };
    auto normalMarkers = [&] {
        if (drawMarkers)
            markerPass(mpx, {}, std::nullopt, std::nullopt, mew);
    };
    auto normalAll = [&] { normalLine(); normalMarkers(); };

    if (pathEffects.empty()) {
        // markerTex markers keep their text path.
        if (drawMarkers && !series_.markerTex.empty()) {
            normalLine();
            auto uni = text::mathTextToUnicode(series_.markerTex);
            const float halfW = series_.size * 0.3f * float(uni.size());
            for (auto p : mpx)
                c.text({p.x - halfW, p.y + series_.size * 0.35f},
                       uni, series_.size, pen.color);
            return;
        }
        normalAll();
        return;
    }

    // mpl patheffects: under-passes in list order.
    for (const auto& fx : pathEffects) {
        Point2D off = fx.offsetPx(dpi);
        switch (fx.kind) {
        case PathEffect::Kind::Normal:
            normalAll();
            break;
        case PathEffect::Kind::Stroke: {
            Color fc = fx.foreground.value_or(pen.color);
            if (hasLine) {
                auto s2 = pen;
                s2.width = fx.strokeWidthPx(series_.lineWidth, dpi);
                s2.color = fc;
                c.polyline(shifted(px, off), s2);
            }
            if (drawMarkers)
                markerPass(mpx, off, faceOv, fc,
                           fx.strokeWidthPx(mew, dpi));
            break;
        }
        case PathEffect::Kind::LineShadow:
        case PathEffect::Kind::PatchShadow: {
            Color sc = fx.shadowFor(pen.color);
            if (hasLine) {
                auto s2 = pen; s2.color = sc;
                c.polyline(shifted(px, off), s2);
            }
            if (drawMarkers)
                markerPass(mpx, off, sc, sc, mew);
            break;
        }
        }
        if (fx.thenNormal) normalAll();
    }
}
void LinePlot::contributeToAutoscale(Viewport& v) const {
    ensureCpuPoints();
    for (const auto& p : series_.points) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}
void LinePlot::contributeToAutoscaleScaled(Viewport& v,
                                           const AxisScale& xscale,
                                           const AxisScale& yscale) const {
    ensureCpuPoints();
    for (const auto& p : series_.points) {
        if (!pointInDomain(p, xscale, yscale) ||
            !std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}
void LinePlot::contributeToAutoscaleGpu(
    render::primitives::ReduceRenderer& reducer, Viewport& v) const {
    auto r = reducer.reduceMinMax2D(renderer_->pointBuffer(), renderer_->pointCount());
    if (!r) { contributeToAutoscale(v); return; }
    v.x.min = std::min(v.x.min, r->minX); v.x.max = std::max(v.x.max, r->maxX);
    v.y.min = std::min(v.y.min, r->minY); v.y.max = std::max(v.y.max, r->maxY);
}
} // namespace volcano::plot
