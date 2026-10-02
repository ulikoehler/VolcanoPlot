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
| line_100 | 80.6 | 95.5 | 15.3 | 0.84x | 5.28x |
| line_2k + scatter + legend | 97.1 | 69.5 | 16.3 | 1.40x | 5.96x |
| scatter_100 | 49.1 | 72.0 | 14.7 | 0.68x | 3.34x |
| bar_10 | 48.2 | 70.0 | 14.2 | 0.69x | 3.40x |
| errorbar_50 | 46.7 | 62.5 | 11.9 | 0.75x | 3.93x |
| boxplot_6 | 47.4 | 71.5 | 13.9 | 0.66x | 3.40x |
| text_50 | 105.9 | 55.4 | 13.3 | 1.91x | 7.98x |
| pie_4 | 21.3 | 19.6 | 13.1 | 1.09x | 1.63x |
| **50 figs × line_2k** (ms/fig) | 71.0 | 169.9 | 5.9 | 0.42x | 12.10x |

### Medium

| case | Agg | mpl+VP | VP (C++) | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_10k | 70.3 | 53.0 | 13.9 | 1.33x | 5.07x |
| line_100k | 73.9 | 69.2 | 14.1 | 1.07x | 5.22x |
| multiline_20×100k | 245.1 | 134.9 | 46.2 | 1.82x | 5.31x |
| scatter_10k | 222.2 | 111.2 | 37.8 | 2.00x | 5.88x |
| scatter_50k | 903.5 | 112.6 | 38.4 | 8.02x | 23.55x |
| hist_100k | 67.2 | 80.8 | 14.3 | 0.83x | 4.70x |
| bar_1k | 257.9 | 242.7 | 14.0 | 1.06x | 18.47x |
| bar_5k | 1118.8 | 1148.0 | 16.2 | 0.97x | 69.26x |
| contourf_100 | 48.9 | 73.9 | 18.3 | 0.66x | 2.67x |
| quiver_30×30 | 71.3 | 68.7 | 13.9 | 1.04x | 5.13x |
| step_50k | 73.3 | 61.9 | 13.1 | 1.18x | 5.60x |
| fill_100k | 81.4 | 68.3 | 39.3 | 1.19x | 2.07x |
| eventplot_2k | 1079.1 | 1077.4 | 16.9 | 1.00x | 63.79x |
| stackplot_5×1k | 146.4 | 64.1 | 14.3 | 2.28x | 10.24x |
| errorbar_5k | 149.2 | 67.4 | 12.5 | 2.21x | 11.91x |
| violin_8×2k | 104.5 | 83.8 | 27.5 | 1.25x | 3.80x |
| stem_2k | 167.5 | 70.1 | 15.6 | 2.39x | 10.77x |

### Large

| case | Agg | mpl+VP | VP (C++) | VP | nat |
|---|---:|---:|---:|---:|---:|
| line_1M | 174.0 | 77.2 | 19.5 | 2.25x | 8.94x |
| line_10M | 1111.6 | 1726.6 | 83.2 | 0.64x | 13.36x |
| line_10M @dpi300 | 1552.5 | 914.5 | 83.7 | 1.70x | 18.56x |
| scatter_200k | 4214.4 | 182.1 | 43.0 | 23.15x | 98.06x |
| scatter_200k @dpi300 | 8743.1 | 745.0 | 69.7 | 11.74x | 125.37x |
| scatter_1M | 12902.9 | 491.6 | 65.2 | 26.25x | 197.94x |
| scatter_2M @dpi400 | 3987.0 | 519.6 | 81.3 | 7.67x | 49.01x |
| scatter per-point s/c 200k | 3688.1 | 156.0 | 49.0 | 23.65x | 75.25x |
| scatter per-point s/c 1M @dpi400 | 41948.5 | 929.6 | 114.1 | 45.12x | 367.70x |
| markers_100k | 117.5 | 52.2 | 19.8 | 2.25x | 5.94x |
| hist_10M | 80.3 | 84.3 | 82.1 | 0.95x | 0.98x |
| fill_1M | 205.7 | 89.1 | 36.9 | 2.31x | 5.58x |
| fill_1M @dpi300 | 410.5 | 198.4 | 59.0 | 2.07x | 6.96x |
| bar_50k | 9001.0 | 11061.8 | 15.4 | 0.81x | 584.35x |
| eventplot_20k | 3999.6 | 6658.1 | 17.1 | 0.60x | 233.79x |
| pcolormesh_2M | 802.3 | 251.3 | 38.4 | 3.19x | 20.89x |
| pcolormesh_2M @dpi300 | 1074.5 | 479.7 | 68.2 | 2.24x | 15.75x |
| pcolormesh_7M @dpi400 | 3769.9 | 6814.3 | 94.8 | 0.55x | 39.78x |
| quadmesh_1M @dpi400 | 921.9 | 732.2 | 64.0 | 1.26x | 14.40x |
| imshow_4M none @dpi300 | 2253.9 | 726.9 | 114.9 | 3.10x | 19.61x |
| imshow_16M none @dpi400 | 4403.6 | 2155.3 | 270.5 | 2.04x | 16.28x |
| imshow_9M bilinear | 1719.2 | 3379.3 | 177.4 | 0.51x | 9.69x |
| contourf_400 | 53.6 | 64.2 | 71.9 | 0.83x | 0.75x |

### When VolcanoPlot wins / loses

**mpl+VP faster than Agg:** scatter ≥10k (8–45×), per-point `s=`/`c=`
scatter (24–45×), `imshow` nearest (2–3×), `pcolormesh`/`quadmesh`
(1.3–3.2×), `fill_between`/`stackplot`/`step`/errorbar/stem (1.2–2.4×),
`line_10M` at dpi300 (1.7× via C++ envelope decimation).

**mpl+VP slower than Agg:** per-artist Python overhead dominates —
`bar` (0.8×), `eventplot` (0.6×), small-figure fixed overhead
(~0.7×), `contourf` (0.66–0.83×), `imshow` bilinear (0.51×, resampling
is matplotlib-side), `pcolormesh_7M` (0.55×), `line_10M` at dpi100
(0.64×, numpy transform cost inside mpl's `Line2D`), repeated figures
(`multi_50x` 0.42× — per-figure mpl churn).

**VP (C++) faster than Agg** on virtually everything — biggest wins where
matplotlib's per-element artist objects dominate: `bar_50k` 584×,
`scatter_sz_1M` 368×, `eventplot_20k` 234×, `scatter_1M` 198×; the
repeated-figure case hits 12× via `savefigAsync` encode overlap.
Exceptions: dense `contourf` (0.75×, CPU-bound tessellation) and
`hist_10M` (0.98× — parity; GPU binning is opt-in, `VOLCANO_GPU_HIST=1`,
since the sample upload outweighs the 8-thread CPU count on dGPU).

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
