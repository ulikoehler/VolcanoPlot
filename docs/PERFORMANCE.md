# Performance: VolcanoPlot vs. matplotlib

This page documents **when VolcanoPlot is faster or slower than stock
matplotlib**, measured across the three ways you can render:

| Stack | What runs |
|---|---|
| **mpl (Agg)** | Stock matplotlib, `Agg` backend — the CPU-rasterization baseline |
| **mpl+VP** | `matplotlib.use("module://volcanoplot.mpl_backend")` — the full matplotlib frontend (artists, transforms, layout, ticks) with VolcanoPlot as the rasterization backend |
| **volcanoplot** | `import volcanoplot` — the native API: VolcanoPlot's own lightweight frontend *and* renderer |

Understanding the split matters: **mpl+VP** replaces only the rasterizer —
matplotlib still creates artists, walks collections, applies transforms, and
emits draw calls in Python. **volcanoplot** skips all of that: data goes from
NumPy arrays to GPU buffers through a thin binding layer. The two are not
always directly comparable (the native API is not a 1:1 matplotlib clone), but
the performance shape is instructive.

---

## TL;DR rules of thumb

| Workload | Fastest | Typical margin |
|---|---|---|
| Tiny figures (≤ ~1k elements) | **Agg / tie** | mpl+VP ~10–40 % slower (ms-scale); native mixed |
| Lines ≥ ~1M points (or many series) | **mpl+VP / native** | ~1.3–2.7× faster |
| Scatter ≥ ~10k points | **mpl+VP / native** | ~2× at 10k → **10–18×** at 1M |
| Scatter with per-point `s=`/`c=` | **mpl+VP / native** | **11–16×** |
| `fill_between`, `stackplot` | **mpl+VP / native** | ~1.4–3.7× |
| `pcolormesh` / `quadmesh` ≥ 1M cells | **mpl+VP** | ~1.5–2.7× |
| `imshow` `interpolation='none'` | **mpl+VP / native** | ~1.0–1.2× (up to ~8× in draw-only) |
| `imshow` `bilinear` etc. | **Agg** over mpl+VP; **native** over both | mpl resamples CPU-side *before* the backend; native GPU-samples |
| `bar` / `eventplot` / `stem` via mpl | **Agg** (by ~1.1–1.6×) | Per-artist Python cost dominates both backends |
| `bar` / `eventplot` via native | **native by 13–60×** | No per-element artist objects |
| `contourf` on dense grids | **Agg** | Native contourf tessellation is CPU-bound (0.2–0.6×) |
| First figure in a process | **Agg** | One-time Vulkan init ~1–2 s, amortized afterwards |

---

## Methodology

`scripts/bench_matrix.py` builds the *same logical plot* on all three stacks
and times `fig.savefig(...)` end-to-end (record + raster + encode + write) —
the fair comparison for headless/batch use. Each case gets a fresh figure;
numbers are **medians of 3 runs** on a warm process.

```bash
# one process per stack (the mpl backend is process-global)
MPLBACKEND=Agg python3 scripts/bench_matrix.py agg
PYTHONPATH=build/python python3 scripts/bench_matrix.py vp
PYTHONPATH=build/python python3 scripts/bench_matrix.py native
python3 scripts/bench_matrix.py report        # merged table
```

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
| line_100 | 51.6 | 57.5 | 31.5 | 0.90× | 1.64× |
| line_2k + scatter + legend | 58.7 | 92.0 | 104.4 | 0.64× | 0.56× |
| scatter_100 | 44.8 | 59.9 | 57.9 | 0.75× | 0.77× |
| bar_10 | 39.4 | 56.5 | 38.2 | 0.70× | 1.03× |
| errorbar_50 | 40.9 | 60.2 | 97.4 | 0.68× | 0.42× |
| boxplot_6 | 43.0 | 61.0 | 81.3 | 0.71× | 0.53× |
| text_50 | 71.4 | 63.5 | 34.5 | 1.12× | 2.07× |
| pie_4 | 21.0 | 32.1 | 32.2 | 0.65× | 0.65× |
| **50 figures × line_2k** (ms/fig) | 48.1 | 71.4 | 54.4 | 0.67× | 0.88× |

### Medium

| case | mpl (Agg) | mpl+VP | native | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_10k | 34.8 | 58.8 | 33.9 | 0.59× | 1.03× |
| line_100k | 49.1 | 54.0 | 36.4 | 0.91× | 1.35× |
| multiline_20×100k | 246.7 | 189.6 | 295.0 | 1.30× | 0.84× |
| scatter_10k | 199.9 | 108.6 | 99.4 | 1.84× | 2.01× |
| scatter_50k | 683.7 | 154.9 | 128.7 | 4.41× | 5.31× |
| hist_100k | 48.9 | 63.3 | 29.2 | 0.77× | 1.67× |
| bar_1k | 153.5 | 194.9 | 36.0 | 0.79× | 4.27× |
| bar_5k | 618.1 | 665.0 | 45.5 | 0.93× | 13.6× |
| contourf_100 | 42.6 | 69.1 | 59.0 | 0.62× | 0.72× |
| quiver_30×30 | 37.4 | 59.9 | 45.5 | 0.62× | 0.82× |
| step_50k | 53.8 | 63.8 | 40.6 | 0.84× | 1.32× |
| fill_100k | 62.5 | 59.3 | 60.4 | 1.05× | 1.03× |
| eventplot_2k | 522.6 | 755.0 | 44.1 | 0.69× | 11.9× |
| stackplot_5×1k | 80.9 | 59.4 | 39.5 | 1.36× | 2.05× |
| errorbar_5k | 73.0 | 82.6 | 42.6 | 0.88× | 1.71× |
| violin_8×2k | 42.3 | 51.6 | 63.7 | 0.82× | 0.66× |
| stem_2k | 75.6 | 253.5 | 74.0 | 0.30× | 1.02× |

### Large

| case | mpl (Agg) | mpl+VP | native | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_1M | 118.7 | 70.0 | 58.0 | 1.70× | 2.05× |
| line_10M | 677.8 | 254.4 | 257.9 | 2.66× | 2.63× |
| line_10M @ dpi300 | 811.8 | 410.4 | 386.6 | 1.98× | 2.10× |
| scatter_200k | 2459.2 | 317.0 | 205.0 | 7.76× | 12.0× |
| scatter_200k @ dpi300 | 4619.1 | 1089.1 | 889.4 | 4.24× | 5.19× |
| scatter_1M | 10719.0 | 598.5 | 336.8 | 17.9× | 31.8× |
| scatter_2M @ dpi400 | 2992.3 | 1466.3 | 1634.8 | 2.04× | 1.83× |
| scatter_sz_200k (per-point s,c) | 3005.3 | 272.3 | 185.3 | 11.0× | 16.2× |
| scatter_sz_1M @ dpi400 | 32759.8 | 3090.8 | 2212.5 | 10.6× | 14.8× |
| markers_100k (`plot('o')`) | 127.7 | 115.1 | 74.8 | 1.11× | 1.71× |
| hist_10M | 60.3 | 87.2 | 118.8 | 0.69× | 0.51× |
| fill_1M | 180.8 | 110.4 | 48.5 | 1.64× | 3.73× |
| fill_1M @ dpi300 | 406.1 | 216.2 | 197.9 | 1.88× | 2.05× |
| bar_50k | 5592.5 | 6363.3 | 88.2 | 0.88× | **63.4×** |
| eventplot_20k | 2430.4 | 3805.2 | 41.7 | 0.64× | **58.3×** |
| pcolormesh_2M | 686.1 | 253.3 | 594.8 | 2.71× | 1.15× |
| pcolormesh_2M @ dpi300 | 925.3 | 487.0 | 845.5 | 1.90× | 1.09× |
| pcolormesh_7M @ dpi400 | 2943.7 | 1317.3 | 2739.6 | 2.23× | 1.07× |
| quadmesh_1M @ dpi400 | 771.5 | 528.9 | 911.5 | 1.46× | 0.85× |
| imshow_4M none @ dpi300 | 560.6 | 472.3 | 465.0 | 1.19× | 1.21× |
| imshow_16M none @ dpi400 | 1160.8 | 1118.6 | 944.1 | 1.04× | 1.23× |
| imshow_9M bilinear | 690.2 | 827.0 | 342.3 | 0.83× | 2.02× |
| contourf_400 | 38.2 | 77.0 | 172.0 | 0.50× | 0.22× |

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
  the pixel count.

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
  triangles per band per cell on the CPU; Agg's scanline filler is lighter
  for this geometry. Expect ~2–4× slower — a known optimization target.

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

- **`contourf`** (see above — CPU tessellation, 0.2–0.7×).
- **`hist` on huge inputs** (~0.5× at 10M): `np.histogram`-scale binning is
  already C-fast; our auto-bin path adds quantile work on top.
- **`quadmesh`/`pcolormesh` at extreme DPI** — the mpl+VP texture path beats
  the native mesh pipeline on the same data (1.4–2.7× vs 0.9–1.2×).
- **Mixed small complex figures** (`line_2k`+legend+scatter, `errorbar_50`,
  `boxplot_6`, `violin`): fixed per-figure layout + GPU sync can make native
  ~1.5–2.5× slower than Agg at millisecond scale.
- **First figure in a process**: one-time Vulkan init ~1–2 s (device,
  pipelines, shader compile). Amortized across subsequent figures —
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

## Reproducing

See [Methodology](#methodology). The benchmark writes timings to
`/tmp/bench_matrix_<stack>.json` and the rendered images to
`/tmp/bm_<stack>_<case>.png`, so you can eyeball output parity alongside
the numbers.
