# VolcanoPlot

A Vulkan-based GPU-side plotter for C++23 with a Python binding and a
matplotlib backend. Inspired by the WebGPU VolcanoPlot prototype; targets
publication-quality output with matplotlib-style styling.

- **Screen mode** — GLFW window with realtime liveplot (zoom/pan/infinite zoom)
- **Headless mode** — offscreen render + GPU-side PNG/WebP encoding
- **Python** — `import volcanoplot` (native API) or
  `matplotlib.use("module://volcanoplot.mpl_backend")` to render real
  matplotlib figures through Vulkan

## Benchmark results

Three stacks are compared — stock matplotlib (`Agg`), matplotlib with the
VolcanoPlot backend (`mpl+VP`), and the raw C++ API (`VP C++`). Each row
is the **median `savefig` wall time in ms** (3 timed runs after a warm-up
rep; identical input data on all stacks). `VP` = Agg/mpl+VP and `nat` =
Agg/VP-C++: `2.0` means VolcanoPlot is 2× faster, `0.5` means 2× slower.
One-time Vulkan init is measured separately (recorded as `_init`), not
included below.

Measured on an AMD Radeon RX 7800 XT (Mesa RADV), Python 3.14,
matplotlib 3.10.9. Absolute ms vary with hardware/DPI/system load — read
the *shape*, not the spec. See `docs/PERFORMANCE.md` for details.

### Small figures

| case | Agg | mpl+VP | VP (C++) | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_100 | 148.5 | 79.1 | 25.5 | 1.88x | 5.83x |
| line_2k + scatter + legend | 178.9 | 88.4 | 17.8 | 2.02x | 10.04x |
| scatter_100 | 101.1 | 65.1 | 15.0 | 1.55x | 6.72x |
| bar_10 | 93.1 | 51.1 | 15.6 | 1.82x | 5.96x |
| errorbar_50 | 151.5 | 65.8 | 13.4 | 2.30x | 11.32x |
| boxplot_6 | 107.1 | 65.9 | 14.2 | 1.62x | 7.53x |
| text_50 | 285.9 | 67.8 | 20.9 | 4.22x | 13.65x |
| pie_4 | 44.5 | 24.5 | 20.6 | 1.81x | 2.16x |
| **50 figs × line_2k** (ms/fig) | 80.0 | 219.5 | 6.5 | 0.36x | 12.30x |

### Medium

| case | Agg | mpl+VP | VP (C++) | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_10k | 276.8 | 70.8 | 14.8 | 3.91x | 18.65x |
| line_100k | 164.9 | 67.7 | 21.9 | 2.44x | 7.51x |
| multiline_20×100k | 631.8 | 181.2 | 40.3 | 3.49x | 15.66x |
| scatter_10k | 663.2 | 89.7 | 32.9 | 7.39x | 20.18x |
| scatter_50k | 1924.3 | 146.4 | 40.0 | 13.14x | 48.15x |
| hist_100k | 190.6 | 67.4 | 12.9 | 2.83x | 14.77x |
| bar_1k | 585.4 | 254.4 | 11.8 | 2.30x | 49.48x |
| bar_5k | 1138.5 | 1155.5 | 12.1 | 0.99x | 94.34x |
| contourf_100 | 48.6 | 108.0 | 30.5 | 0.45x | 1.60x |
| quiver_30×30 | 78.0 | 83.6 | 30.4 | 0.93x | 2.56x |
| step_50k | 59.6 | 101.6 | 15.6 | 0.59x | 3.81x |
| fill_100k | 67.2 | 77.3 | 41.4 | 0.87x | 1.62x |
| eventplot_2k | 1056.0 | 1199.5 | 16.4 | 0.88x | 64.29x |
| stackplot_5×1k | 175.6 | 111.7 | 13.3 | 1.57x | 13.22x |
| errorbar_5k | 111.3 | 175.8 | 12.4 | 0.63x | 9.00x |
| violin_8×2k | 62.0 | 126.1 | 20.5 | 0.49x | 3.03x |
| stem_2k | 125.6 | 116.2 | 12.0 | 1.08x | 10.45x |

### Large

| case | Agg | mpl+VP | VP (C++) | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_1M | 129.1 | 93.3 | 15.9 | 1.38x | 8.11x |
| line_10M | 865.4 | 418.1 | 54.4 | 2.07x | 15.92x |
| line_10M @dpi300 | 2714.4 | 773.8 | 86.2 | 3.51x | 31.48x |
| scatter_200k | 7359.9 | 280.6 | 54.5 | 26.23x | 135.00x |
| scatter_200k @dpi300 | 7190.3 | 886.8 | 69.2 | 8.11x | 103.92x |
| scatter_1M | 19281.3 | 527.2 | 66.3 | 36.58x | 290.71x |
| scatter_2M @dpi400 | 6796.9 | 653.5 | 86.6 | 10.40x | 78.48x |
| scatter per-point s/c 200k | 4927.5 | 236.8 | 42.5 | 20.81x | 116.00x |
| scatter per-point s/c 1M @dpi400 | 52229.5 | 1159.1 | 123.2 | 45.06x | 424.05x |
| markers_100k | 152.4 | 85.0 | 27.6 | 1.79x | 5.52x |
| hist_10M | 115.3 | 133.6 | 98.0 | 0.86x | 1.18x |
| fill_1M | 213.3 | 149.5 | 74.2 | 1.43x | 2.88x |
| fill_1M @dpi300 | 479.9 | 272.5 | 93.9 | 1.76x | 5.11x |
| bar_50k | 12031.8 | 13390.8 | 25.8 | 0.90x | 465.73x |
| eventplot_20k | 4625.7 | 13651.7 | 33.1 | 0.34x | 139.92x |
| pcolormesh_2M | 941.8 | 502.9 | 39.9 | 1.87x | 23.61x |
| pcolormesh_2M @dpi300 | 1375.4 | 957.2 | 82.0 | 1.44x | 16.77x |
| pcolormesh_7M @dpi400 | 4108.8 | 3652.2 | 104.0 | 1.13x | 39.49x |
| quadmesh_1M @dpi400 | 1134.4 | 1033.9 | 68.0 | 1.10x | 16.69x |
| imshow_4M none @dpi300 | 713.6 | 825.5 | 91.1 | 0.86x | 7.83x |
| imshow_16M none @dpi400 | 2323.5 | 3651.2 | 232.1 | 0.64x | 10.01x |
| imshow_9M bilinear | 1114.0 | 6024.0 | 143.8 | 0.18x | 7.75x |
| contourf_400 | 72.1 | 173.0 | 110.7 | 0.42x | 0.65x |

### When VolcanoPlot wins / loses

**mpl+VP faster than Agg:** scatter ≥10k (7.4–45×), per-point `s=`/`c=`
scatter (21–45×), `pcolormesh`/`quadmesh` (1.1–24×), `line` plots
(1.4–3.9×, `line_10M` 2.1–3.5× via C++ envelope decimation), `hist_100k`
(2.8×), `stackplot`/`stem` (1.1–1.6×), small-figure cases (1.5–4.2×).

**mpl+VP slower than Agg:** per-artist Python overhead dominates —
`bar` ≥5k (0.9–1.0×), `eventplot` (0.34–0.88×), `contourf` (0.42–0.45×),
`imshow` (0.18–0.86×, resampling/encoding is matplotlib-side), `step`,
`errorbar`, `violin`, `quiver`, `fill_100k` (0.5–0.9×), `hist_10M`
(0.86×), repeated figures (`multi_50x` 0.36× — per-figure mpl churn).

**VP (C++) faster than Agg** on virtually everything — biggest wins where
matplotlib's per-element artist objects dominate: `bar_50k` 466×,
`scatter_sz_1M` 424×, `scatter_1M` 291×, `eventplot_20k` 140×; the
repeated-figure case hits 12× via `savefigAsync` encode overlap.
Exceptions: dense `contourf` (0.65×, CPU-bound tessellation).
`hist_10M` is now a slight win (1.18×); GPU binning remains opt-in
(`VOLCANO_GPU_HIST=1`).

### Reproduce

The full three-stack run dumps identical input data, runs each stack in its
own process, and writes all results (timings + per-case PNGs) into
`gallery/benchmark/`:

```bash
python3 scripts/bench_matrix.py all      # dump + agg + vp + cpp + report
```

Or step by step:

```bash
python3 scripts/bench_matrix.py dump     # identical .npy inputs for all stacks
MPLBACKEND=Agg python3 scripts/bench_matrix.py agg
PYTHONPATH=build/python python3 scripts/bench_matrix.py vp
python3 scripts/bench_matrix.py cpp      # builds + runs example_bench_matrix
python3 scripts/bench_matrix.py report   # table + benchmark_report.md
```

See `docs/PERFORMANCE.md` for the methodology (warm-up/`_init` handling,
steady-state metric).

## Build

```bash
sudo apt-get install -y libvulkan-dev libglfw3-dev libshaderc-dev \
    glslang-dev spirv-tools vulkan-validationlayers \
    vulkan-utility-libraries-dev libpng-dev libwebp-dev \
    libfreetype-dev libharfbuzz-dev mesa-vulkan-drivers python3-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/tests/volcano_tests
```

See `AGENTS.md` for the full developer guide and `docs/` for design docs.
