# Performance: VolcanoPlot vs. matplotlib

This page documents **when VolcanoPlot is faster or slower than stock
matplotlib**, measured across the three ways you can render:

| Stack | What runs |
|---|---|
| **mpl (Agg)** | Stock matplotlib, `Agg` backend — the CPU-rasterization baseline |
| **mpl+VP** | `matplotlib.use("module://volcanoplot.mpl_backend")` — the full matplotlib frontend (artists, transforms, layout, ticks) with VolcanoPlot as the rasterization backend |
| **VP (C++)** | `example_bench_matrix` — the raw C++ API with zero Python: figures, axes and plot layers built directly against `volcano::plot` |

Understanding the split matters: **mpl+VP** replaces only the rasterizer —
matplotlib still creates artists, walks collections, applies transforms, and
emits draw calls in Python. **VP (C++)** skips all of that: data goes from
input arrays to GPU buffers with no frontend interpreter at all. The two are
not always directly comparable (the C++ API is not a 1:1 matplotlib clone),
but the performance shape is instructive.

(A fourth stack — `import volcanoplot`, the same C++ engine driven through
the pybind11 bindings — can still be run manually via
`bench_matrix.py native`; the pybind layer only adds figure-setup overhead,
so its numbers track the C++ column closely.)

---

## TL;DR rules of thumb

| Workload | Fastest | Typical margin |
|---|---|---|
| Tiny figures (≤ ~1k elements) | **Agg / tie** | mpl+VP ~10–40 % slower (ms-scale); native mixed |
| Lines ≥ ~1M points (or many series) | **mpl+VP / native** | ~1.3–2.7× faster |
| Scatter ≥ ~10k points | **mpl+VP / native** | ~2× at 10k → **10–18×** at 1M |
| Scatter with per-point `s=`/`c=` | **mpl+VP / native** | **11–16×** |
| `fill_between`, `stackplot` | **mpl+VP / native** | ~1.4–3.7× |
| `pcolormesh` / `quadmesh` ≥ 1M cells (uniform) | **mpl+VP / native** | ~1.5–2.7× (texture + quad vs per-cell polygons) |
| `imshow` `interpolation='none'` | **mpl+VP / native** | ~1.0–1.2× (up to ~8× in draw-only) |
| `imshow` `bilinear` etc. | **Agg** over mpl+VP; **native** over both | mpl resamples CPU-side *before* the backend; native GPU-samples |
| `bar` / `eventplot` / `stem` via mpl | **Agg** (by ~1.1–1.6×) | Per-artist Python cost dominates both backends |
| `bar` / `eventplot` via native | **native by 13–60×** | No per-element artist objects |
| `contourf` on dense grids | **Agg** | Native contourf tessellation is CPU-bound (0.2–0.6×) |
| First figure in a process | **Agg** | One-time Vulkan init ~1–2 s, amortized afterwards |

---

## Methodology

`scripts/bench_matrix.py` builds the *same logical plot* — identical input
data, figure size (6.4×4.8in) and dpi — on all three stacks and times
`fig.savefig(...)` end-to-end (record + raster + encode + write) — the fair
comparison for headless/batch use. For the C++ stack the Python harness
first dumps every case's input arrays to `data/<case>/*.npy`
(`bench_matrix.py dump`), which `example_bench_matrix` replays verbatim, so
all three stacks see byte-identical inputs.

**Vulkan init is measured separately, never included in the case metrics** —
the reported numbers are steady-state ("the user wants to make 100 plots").
Each stack runs a global warm-up figure first (recorded as the `_init`
pseudo-case), and each case gets one untimed rep before the timed reps.
Numbers are **medians of 3 timed runs** on a warm process.

### Timing boundary

The per-case metric is `savefig` wall time only:

- **agg / vp**: `fig.savefig(path)` — artist traversal, recording,
  rasterization, PNG encode, file write. Figure *construction*
  (`plt.figure`, `ax.plot`, ...) happens before the timer starts.
- **cpp**: `renderer.savefig(fig, path)` — `prepare` (data upload) +
  `renderFrame` + readback + encode + write. `plot::Figure`/`Axes`/plot
  construction and `.npy` data loading happen before the timer starts.

Exception: the `multi_50x_line2k` workload deliberately times the *whole*
loop (figure construction + plot calls + savefig + close, total ÷ 50) —
that is the "100 plots" amortized number.

### What `_init` covers

`_init` = wall time of one identical warm-up figure (line + scatter + bar +
imshow + text, 640×480) rendered once per stack before any case runs — i.e.
"cost of the first figure". What that physically includes per stack:

| stack | `_init` contains |
|---|---|
| `agg` | matplotlib's first-save overhead only (no Vulkan): backend state, font cache, first `FigureCanvasAgg` draw. ~0.1 s. |
| `vp` | pooled headless canvas creation for 640×480 → Vulkan instance/device bring-up, shader compile + pipeline creation for the primitives the figure touches, font atlas raster+upload, first readback+encode. ~2.7 s. |
| `cpp` | `sharedGpuContext()` + `HeadlessBackend` + `Renderer` construction + the same warm-up figure's `savefig`. ~3.6 s. |

Deliberately **not** in `_init` (and therefore not in any metric):

- process start — module imports, `matplotlib.use(...)`, shared-library
  loading. `_init` is defined as first-*figure* cost, not interpreter
  start-up. Note the GPU context itself *is* inside `_init` for both Vulkan
  stacks: it is created lazily by the first `savefig` (canvas pool) on `vp`
  and inside the warm-up's `rendererFor` on `cpp`.
- process teardown.

### What rep 0 absorbs

Each case runs rep 0 untimed on **all** stacks so that first-use costs stay
out of the metric:

- plot-type-specific pipeline compiles the global warm-up figure didn't
  touch (e.g. quiver/violin/contour pipelines),
- matplotlib-side first-touch caches on the agg/vp stacks,
- OS/driver-level page-in for that case's buffers.

The warm-up figure only touches the *common* pipelines (line, point, bar,
image, text), so `vp`'s `_init` is somewhat smaller than `cpp`'s, which
builds a wider pipeline set up front — both measure "first figure", the
difference is just which pipelines it happened to compile.

### Identical inputs

`bench_matrix.py dump` replays each case's `build(ax)` against a recording
stub and dumps every array argument to `data/<case>/*.npy`
(`manifest.tsv` maps `case → dpi, data dir, category`; dpi variants share
one data dir since the data doesn't depend on dpi). The C++ bench loads
those files, so all three stacks draw **byte-identical** data at identical
figure size/dpi — the same RNG seeds produce the same arrays everywhere.

```bash
# everything: dump data, run all three stacks sequentially, merge report
python3 scripts/bench_matrix.py all

# or step by step (one process per stack — the mpl backend is process-global)
python3 scripts/bench_matrix.py dump          # data/*.npy for the C++ bench
MPLBACKEND=Agg python3 scripts/bench_matrix.py agg
PYTHONPATH=build/python python3 scripts/bench_matrix.py vp
python3 scripts/bench_matrix.py cpp           # builds + runs example_bench_matrix
python3 scripts/bench_matrix.py report        # merged table + markdown
```

All output lands in `gallery/benchmark/` (override with `--outdir`):

| File | Content |
|---|---|
| `<case>_<stack>.png` | rendered output of every case on every stack — direct visual comparison |
| `data/<case>/*.npy` | the shared input arrays (manifest: `data/manifest.tsv`) |
| `bench_matrix_<stack>.json` | per-case timings + init |
| `benchmark_report.md` | merged report: timing table + embedded per-case PNGs |

`scripts/bench_mpl_backend.py` (finer draw-level breakdown) and
`scripts/bench_native_vs_mpl.py` cover related workloads.

**Ratio convention:** `VP` = ms(Agg) / ms(mpl+VP) — `2.0` means VolcanoPlot
is **2× faster**, `0.5` means **2× slower**. Same for `nat`.

**Caveat — read before quoting numbers:** the table below was produced on a
32-core machine under loadavg ~95 with an NVIDIA GPU. Absolute ms are
inflated and noisy; the *ratios* are stable across repeats. GPU/driver
model, DPI, figure size, data distribution, and system load all shift the
crossover points — treat this as the *shape* of the behavior, not a spec.

## Results

Median `savefig` wall time, milliseconds (smaller = better).

### Small figures — fixed overhead dominates

| case | mpl (Agg) | mpl+VP | native | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_100 | 80.6 | 95.5 | 15.3 | 0.84× | 5.28× |
| line_2k + scatter + legend | 97.1 | 69.5 | 16.3 | 1.40× | 5.96× |
| scatter_100 | 49.1 | 72.0 | 14.7 | 0.68× | 3.34× |
| bar_10 | 48.2 | 70.0 | 14.2 | 0.69× | 3.40× |
| errorbar_50 | 46.7 | 62.5 | 11.9 | 0.75× | 3.93× |
| boxplot_6 | 47.4 | 71.5 | 13.9 | 0.66× | 3.40× |
| text_50 | 105.9 | 55.4 | 13.3 | 1.91× | 7.98× |
| pie_4 | 21.3 | 19.6 | 13.1 | 1.09× | 1.63× |
| **50 figures × line_2k** (ms/fig) | 71.0 | 169.9 | 5.9 | 0.42× | 12.10× |

### Medium

| case | mpl (Agg) | mpl+VP | native | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_10k | 70.3 | 53.0 | 13.9 | 1.33× | 5.07× |
| line_100k | 73.9 | 69.2 | 14.1 | 1.07× | 5.22× |
| multiline_20×100k | 245.1 | 134.9 | 46.2 | 1.82× | 5.31× |
| scatter_10k | 222.2 | 111.2 | 37.8 | 2.00× | 5.88× |
| scatter_50k | 903.5 | 112.6 | 38.4 | 8.02× | 23.55× |
| hist_100k | 67.2 | 80.8 | 14.3 | 0.83× | 4.70× |
| bar_1k | 257.9 | 242.7 | 14.0 | 1.06× | 18.47× |
| bar_5k | 1118.8 | 1148.0 | 16.2 | 0.97× | 69.26× |
| contourf_100 | 48.9 | 73.9 | 18.3 | 0.66× | 2.67× |
| quiver_30×30 | 71.3 | 68.7 | 13.9 | 1.04× | 5.13× |
| step_50k | 73.3 | 61.9 | 13.1 | 1.18× | 5.60× |
| fill_100k | 81.4 | 68.3 | 39.3 | 1.19× | 2.07× |
| eventplot_2k | 1079.1 | 1077.4 | 16.9 | 1.00× | 63.79× |
| stackplot_5×1k | 146.4 | 64.1 | 14.3 | 2.28× | 10.24× |
| errorbar_5k | 149.2 | 67.4 | 12.5 | 2.21× | 11.91× |
| violin_8×2k | 104.5 | 83.8 | 27.5 | 1.25× | 3.80× |
| stem_2k | 167.5 | 70.1 | 15.6 | 2.39× | 10.77× |

### Large

| case | mpl (Agg) | mpl+VP | native | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_1M | 174.0 | 77.2 | 19.5 | 2.25× | 8.94× |
| line_10M | 1111.6 | 1726.6 | 83.2 | 0.64× | 13.36× |
| line_10M @ dpi300 | 1552.5 | 914.5 | 83.7 | 1.70× | 18.56× |
| scatter_200k | 4214.4 | 182.1 | 43.0 | 23.15× | 98.06× |
| scatter_200k @ dpi300 | 8743.1 | 745.0 | 69.7 | 11.74× | 125.37× |
| scatter_1M | 12902.9 | 491.6 | 65.2 | 26.25× | 197.94× |
| scatter_2M @ dpi400 | 3987.0 | 519.6 | 81.3 | 7.67× | 49.01× |
| scatter_sz_200k (per-point s,c) | 3688.1 | 156.0 | 49.0 | 23.65× | 75.25× |
| scatter_sz_1M @ dpi400 | 41948.5 | 929.6 | 114.1 | 45.12× | 367.70× |
| markers_100k (`plot('o')`) | 117.5 | 52.2 | 19.8 | 2.25× | 5.94× |
| hist_10M | 80.3 | 84.3 | 82.1 | 0.95× | 0.98× |
| fill_1M | 205.7 | 89.1 | 36.9 | 2.31× | 5.58× |
| fill_1M @ dpi300 | 410.5 | 198.4 | 59.0 | 2.07× | 6.96× |
| bar_50k | 9001.0 | 11061.8 | 15.4 | 0.81× | 584.35× |
| eventplot_20k | 3999.6 | 6658.1 | 17.1 | 0.60× | 233.79× |
| pcolormesh_2M | 802.3 | 251.3 | 38.4 | 3.19× | 20.89× |
| pcolormesh_2M @ dpi300 | 1074.5 | 479.7 | 68.2 | 2.24× | 15.75× |
| pcolormesh_7M @ dpi400 | 3769.9 | 6814.3 | 94.8 | 0.55× | 39.78× |
| quadmesh_1M @ dpi400 | 921.9 | 732.2 | 64.0 | 1.26× | 14.40× |
| imshow_4M none @ dpi300 | 2253.9 | 726.9 | 114.9 | 3.10× | 19.61× |
| imshow_16M none @ dpi400 | 4403.6 | 2155.3 | 270.5 | 2.04× | 16.28× |
| imshow_9M bilinear | 1719.2 | 3379.3 | 177.4 | 0.51× | 9.69× |
| contourf_400 | 53.6 | 64.2 | 71.9 | 0.83× | 0.75× |

---

## When mpl+VP beats Agg — and why

**The pattern: VolcanoPlot wins whenever the per-primitive cost is high and
the primitive count is large.** Agg rasterizes every marker, every line
segment, every mesh cell on the CPU, one pixel at a time. The VolcanoPlot
backend translates matplotlib draw calls into batched GPU operations:

- **Scatter / markers → SDF point sprites.** Each marker is *one vertex*
  carrying position, color, and pixel size; a fragment shader evaluates the
  shape analytically (circles, squares, triangles, stars — the full mpl
  marker set). Agg rasterizes each marker as a filled polygon: at 1M points
  that is ~0.6 s vs ~11 s. The win is largest with per-point `s=`/`c=`
  arrays (`scatter_sz_*`), which ride along as vertex attributes.

- **Lines → instanced GPU polylines + data-space decimation.** Before
  upload, an x-monotonic line on a linear scale is reduced to a per-pixel-
  column min/max *envelope*: 10M points on a ~1000-px-wide axes become
  ~2000 vertices — visually identical, since every skipped point lies inside
  a pixel column already covered. Agg walks and rasterizes all 10M segments.

- **`pcolormesh`/`quadmesh`/`imshow(interpolation='none')` → texture
  upload.** The cell grid becomes a texture + one quad sampled in the
  fragment shader; Agg fills every cell as a polygon. This is also why
  high-DPI renders scale gracefully — upload size depends on the data, not
  the pixel count. On the *native* API, `pcolormesh` uses the same
  texture path whenever the grid is uniform (flat shading, no cell
  borders, no under/over/opaque-bad colormap colors); irregular grids
  keep the per-cell geometry path.

- **`fill_between`/`stackplot` → column-envelope fills.** An x-monotonic
  band is emitted as one quad per pixel column instead of a megabyte-sized
  polygon — O(axes width) primitives instead of O(n).

- **Collections and small paths → ordered batching.** Consecutive
  same-style fills (hist bins, bar rects) merge into one triangle batch;
  consecutive same-style strokes (eventplot rows) merge into one polyline
  batch. Draw order is preserved exactly — only *adjacent* same-kind
  operations merge, so overlapping fill+stroke patches are unaffected.

## When mpl+VP loses to Agg — and why

- **Small figures.** A frame pays Vulkan command recording, submission, and
  synchronization overhead that Agg's in-process C rasterizer doesn't. On
  `line_100`-scale work this makes mpl+VP ~10–40 % slower — a few
  milliseconds on an already-fast operation.

- **`bar`, `eventplot`, `stem` — matplotlib-side artist storms.** mpl
  creates one `Rectangle` patch per bar and one `LineCollection` per
  eventplot/stem row, then calls `.draw()` on each — tens of thousands of
  Python-level calls *before* any backend code runs. Both backends pay it;
  our per-call Python overhead is somewhat heavier than Agg's C fast path,
  so these can land ~1.2–1.6× slower (stem up to ~3×) despite batching.
  **This is the case where the native API is categorically better** — it has
  no per-element artist objects (see below).

- **`imshow` with `bilinear`/`bicubic`/… interpolation.** matplotlib
  software-resamples the image inside `AxesImage.draw` *before* calling
  `draw_image` — the backend only receives already-resampled pixels
  (`_check_unsampled_image` only fires for `interpolation='none'`). The
  resample is mpl-side cost no backend can remove. Use
  `interpolation='none'` (or `'nearest'`) to hit the fast path — at high DPI
  the visual difference is small for dense images.

- **`contourf` on dense grids.** The native contour tessellation emits
  triangles per band per cell on the CPU (row-parallel across up to 8
  threads for large grids); Agg's scanline filler is still lighter for
  this geometry. Expect ~2–4× slower — a known optimization target.

- **Hatches, dashed paths, disjointed path soups** bypass the batching fast
  paths by design (draw-order correctness comes first) and pay per-path
  overhead.

- **Repeated many-figure workloads** (`multi_50x_line2k`: 0.67×): per-figure
  canvas setup costs a few ms more than Agg's, so batch figure generation of
  *small* plots is slower even though per-draw wins exist.

## When `volcanoplot` (native) beats *both*

The native API skips matplotlib entirely: no `Artist` objects, no transform
stack per element, no Python draw loop — NumPy → GPU buffer directly. It is
fastest exactly where mpl's frontend is the bottleneck:

- **`bar_50k` / `eventplot_20k`**: ~40–90 ms native vs 2.4–6.4 s on the mpl
  stacks — mpl spends nearly all of that creating and drawing individual
  artists, which no backend choice can bypass.
- **`hist`, `text`, `pie`, medium `bar`/`errorbar`/`boxplot`-adjacent work**:
  no frontend overhead, so even modest data sizes land ahead.
- **`imshow` interpolated**: native samples the texture on the GPU — the
  `bilinear` case mpl+VP loses to Agg (0.83×) is a 2× *win* natively,
  because there is no mpl-side resample in front of it.

The flip side: **native is a different API.** It is intentionally
matplotlib-shaped (`vp.figure()` → `ax.plot(...)`) but not a drop-in
replacement — signatures and edge semantics differ. When porting a script,
"same logical plot" ≠ "same API call"; benchmark your actual workload rather
than trusting this table.

## When native loses

- **`contourf`** (see above — CPU tessellation, row-parallel but still
  behind Agg's scanline filler, ~0.75×).
- **`hist` on huge inputs**: `np.histogram`-scale binning is already
  C-fast. Our binning uses `minmax`/`nth_element` quantiles and parallel
  counting above 1M samples — parity at 10M (~0.98×). A compute-shader
  binner exists but is opt-in (`VOLCANO_GPU_HIST=1`): uploading the
  samples to a host-visible buffer costs more than the CPU count on
  discrete GPUs; it may win on unified-memory/integrated setups.
- **`quadmesh`/`pcolormesh` on irregular grids** — only uniform grids
  take the texture path; irregular edges still tessellate per cell
  (6 verts/cell uploads dominate at multi-million cells).
- **Mixed small complex figures** (`line_2k`+legend+scatter, `errorbar_50`,
  `boxplot_6`, `violin`): fixed per-figure layout + GPU sync can make native
  ~1.5–2.5× slower than Agg at millisecond scale.
- **First figure in a process**: one-time Vulkan init ~0.8–1.7 s
  (device, pipelines; SPIR-V modules are cached on disk under
  `$XDG_CACHE_HOME/volcanoplot/shaders` so repeat runs skip shaderc).
  Amortized across subsequent figures —
  irrelevant for batch jobs, noticeable for one-shot plots.
- **Exotic markers** (`marker=Path(...)`, TeX `'$…$'` markers, unfilled or
  half-filled styles, distinct `markeredgecolor`): these take the
  tessellated per-marker path instead of point sprites — correct but
  O(n·verts).

## Practical guidance

- **Migrating an existing mpl script?** Try
  `matplotlib.use("module://volcanoplot.mpl_backend")` first — zero code
  changes, keeps mpl's layout/ticks/legend machinery. Expect wins on
  scatter/line/mesh-heavy figures; don't expect wins on `bar`-heavy or
  `contourf`-heavy ones.
- **New code / batch pipelines / big data?** Use `import volcanoplot`
  directly — fastest path, no mpl overhead, and headless `savefig` is the
  primary use case.
- **Interactive?** `plt.show()` works under the mpl backend (GLFW window)
  with zoom/pan routed back through mpl.
- **Mixed workloads:** since the backend is process-global, a pragmatic
  pattern is a subprocess per backend, or pick the backend for the dominant
  workload and accept the small-plot penalty.

## Tuning knobs and async encode

Measured on this machine (RX 7800 XT; see `bench_encode`):

- **PNG deflate level** — `savefig` defaults PNG to zlib level 3
  (~2.4× faster encode than level 6, ~50% larger files on plot frames;
  the encoder stage was ~14 ms/figure of a ~16 ms savefig floor).
  Override per-process with `VOLCANO_PNG_LEVEL=0..9` or per-call via
  `SaveOptions::compressionLevel`.
- **`Renderer::savefigAsync`** — runs the CPU encode + file write on a
  worker thread while the next figure renders. In the 50-figure bench
  (`multi_50x`) this took the native stack to ~6 ms/figure. The file is
  complete when the returned `future` resolves.
- **GPU-hybrid encoders are opt-in** (`VOLCANO_GPU_ENCODERS=1`): PNG
  scanline filtering / JPEG+WebP YUV conversion run on-GPU, but the
  host-visible-buffer round trip loses to the CPU encoders on discrete
  GPUs (measured: PNG 58 vs 14 ms @640×480; JPEG hybrid 29 vs 1.7 ms).
  They exist for unified-memory targets.
- **`VOLCANO_GPU_HIST=1`** — GPU atomic histogram binning (same story:
  slower than the 8-thread CPU count on dGPU, opt-in).
- **SPIR-V disk cache** — compiled shaders persist under
  `$XDG_CACHE_HOME/volcanoplot/shaders` (or `$VOLCANO_CACHE_DIR`);
  `_init` dropped ~3.6 s → ~0.8–1.7 s.

`examples/bench_encode` (built as `build/examples/bench_encode`)
isolates the encode stage: fixed plot/noise frames at 640×480 and
1920×1440 through every encoder, reporting median ms + output size.

## Reproducing

See [Methodology](#methodology). The benchmark writes timings to
`gallery/benchmark/bench_matrix_<stack>.json`, the rendered images to
`gallery/benchmark/<case>_<stack>.png`, and a merged
`gallery/benchmark/benchmark_report.md` (table + side-by-side images), so
you can eyeball output parity alongside the numbers.
