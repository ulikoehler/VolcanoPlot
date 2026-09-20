# VolcanoPlot

Vulkan-accelerated, matplotlib-compatible plotting for C++23 (and Python).

VolcanoPlot renders publication-quality figures on the GPU — raster (PNG/WebP)
and vector (SVG/PDF) output — while mirroring the matplotlib API and visual
semantics: same defaults, locators, legend metrics, and layout rules.

## Quick start (C++)

```cpp
#include <volcano/plot/Plot.hpp>
using namespace volcano;

plot::Figure fig(800, 600, 100.0f);
auto* ax = fig.addAxes();
ax->plot({0, 1, 2, 3}, {0, 1, 0, 1});
ax->setTitle("Hello");
fig.renderPng("out.png");
```

## Quick start (Python)

```bash
cmake -B build -DVOLCANO_BUILD_PYTHON=ON
cmake --build build -j4
```

```python
import volcanoplot as vp
fig = vp.Figure(figsize=(6.4, 4.8), dpi=150)
ax = fig.add_axes()
ax.plot([0, 1, 2, 3], [0, 1, 0, 1], label="line", color="C3")
ax.set_xlabel("x"); ax.legend()
fig.savefig("out.png")
```

## Features

- 60+ plot types: line, scatter, bar (grouped/stacked/3D), hist, boxplot,
  violin, contour/contourf, pcolormesh, imshow, quiver, streamplot, stem,
  step, errorbar, pie, polar + map projections, 3D surface/scatter/bar/voxels
- Matplotlib-parity ticks, legends, colorbars, spines, mathtext
- GPU tessellation (pcolormesh), GPU autoscale, instanced markers
- Headless rendering (works on lavapipe), optional SDL3 live window
- Animation: APNG/GIF/WebP writers with GPU PNG filtering
- Python bindings with numpy/pandas/datetime/categorical ingestion

See [Gallery](gallery.md) for side-by-side matplotlib comparisons and
[Microfeatures](MICROFEATURES.md) for the per-feature parity checklist.
