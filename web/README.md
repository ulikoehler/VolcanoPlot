# @volcanoplot/web

GPU-accelerated plotting for the browser — the [VolcanoPlot](../README.md)
plotting engine (C++23, matplotlib-style) compiled to WebAssembly and
rendered through WebGPU/WGSL, with an automatic Canvas2D fallback.

All plot layout, tick location, transforms, styling, and geometry
tessellation happen in the same C++ code that drives the native Vulkan
backend. The WASM module emits a backend-neutral op stream (`VPOP`) that
a small TypeScript interpreter replays — either onto WebGPU pipelines or
onto a 2D canvas. No plotting logic is reimplemented in JS.

## Quick start

```ts
import { createCanvas } from '@volcanoplot/web';
import createModule from '@volcanoplot/web/volcanoplot.js';

const vp = await createCanvas(
    document.querySelector('canvas')!, createModule);

const N = 1024, xs = new Float32Array(N), ys = new Float32Array(N);
for (let i = 0; i < N; i++) { xs[i] = i / (N - 1); ys[i] = Math.sin(i * 0.05); }
vp.line(xs, ys, '#e00000');
vp.render();
```

No WebGPU? `createCanvas` automatically falls back to a Canvas2D replay of
the same op stream (markers become circles, no 3D, no `func()` — 2D plots
otherwise look the same).

## API

`createCanvas(canvas, moduleFactory) → Promise<VolcanoCanvas>`

`VolcanoCanvas` methods:

| Method | Notes |
|---|---|
| `line(xs, ys, color?)` | polyline; `color` is CSS/hex |
| `scatter(xs, ys, color?)` | point markers |
| `func(body, xMin?, xMax?, color?)` | GPU-evaluated `y = f(x)`; `body` is a GLSL/WGSL expression in `x`, e.g. `'sin(10.0*x)'` |
| `bar(heights, labels?, color?)` / `hist(samples, bins?, color?)` / `pie(values, labels?)` | categorical plots |
| `heatmap(values, w, h, cmap?)` / `hist2d(xs, ys, bins?, cmap?)` / `kde(xs, ys, cmap?)` / `pcolormesh(xs, ys, c, cols, rows)` / `matshow(data, rows, cols)` / `pcolorfast(C, cols, rows, x0?, x1?, y0?, y1?)` / `spy(data, rows, cols)` | 2D fields; `cmap` e.g. `'viridis'` |
| `surface(values, w, h, elev?, azim?)` / `wireframe(values, w, h, elev?, azim?)` / `trisurf(xs, ys, zs, elev?, azim?)` | 3D plots (WebGPU only) |
| `contour(values, w, h, levels?, cmap?)` / `tricontour(xs, ys, zs)` / `stem(xs, ys)` / `quiver(xs, ys, us, vs)` / `errorbar(xs, ys, err, color?)` / `hexbin(xs, ys)` / `boxplot(groups)` / `violin(groups, w?, box?, color?)` / `stackplot(xs, ys[])` / `fill(xs, ys, color?)` / `tripcolor(xs, ys, zs)` / `triplot(xs, ys)` / `streamplot(us, vs, w, h)` / `brokenBarh(segs)` | more plot types |
| `specgram(s, fs?)` / `spectrum(s, fs?)` / `psd(s, fs?)` / `csd(x, y, fs?)` / `xcorr(x, y)` / `cohere(x, y, fs?)` | signal-processing plots (FFT in C++) |
| `xlim(l, h)` `ylim(l, h)` `xscale(n)` `yscale(n)` `title(t)` `xlabel(t)` `ylabel(t)` `grid(on?)` `suptitle(t)` | axes styling |
| `axhline(y, c?, w?)` `axvline(x, c?, w?)` `axhspan(y1, y2, c?)` `axvspan(x1, x2, c?)` `hlines(ys, x0, x1, c?, w?)` `vlines(xs, y0, y1, c?, w?)` | reference geometry |
| `legend(loc?)` `colorbar()` `text(x, y, s, coords?)` | mpl decoration |
| `enableInteraction(on?)` | left-drag pan + scroll zoom (mpl Navigation) |
| `toSvg()` | vector/SVG export of the same op stream |
| `setData(handle, xs, ys)` | in-place update of the series behind a plot handle — the realtime/oscilloscope fast path; call `renderIfStale()` per frame |
| `subplot(nrows, ncols, index)` | mpl-style 1-based subplot; selects it as the current axes |
| `axes(i)` | select an existing axes by index |
| `render()` / `renderIfStale()` | render + replay the op stream |
| `capture()` | offscreen render → `Uint8Array` RGBA8 pixels (test/debug) |
| `resize()` / `syncSize()` | DPR-aware canvas sizing |
| `onDeviceLost` / `destroy()` | WebGPU device-lifetime handling |

All `ArrayLike<number>` inputs accept `Float32Array`, `number[]`, etc.
Data is staged into WASM memory for the duration of the call — zero-copy
from the C++ side.

## Demos

- `demo/smoke.html` — line plot
- `demo/scope.html` — 1 kHz-style streaming oscilloscope (`setData` + rAF)
- `demo/multi.html?kind=…` — every supported plot type
- `demo/fallback.html` — forced Canvas2D path

Serve `web/` statically and open `demo/*.html`; the WASM side files
(`volcanoplot.wasm`, `volcanoplot.data`) resolve relative to the module.

## How it works

```
JS data ──► WASM heap ──► C++ Figure/Axes/plot engine
                              │  layout, ticks, transforms, tessellation
                              ▼
                     VPOP op stream (byte buffer)
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
     WebGPU interpreter              Canvas2D interpreter
     WGSL pipelines, compute         CPU replay, 2D canvas
     mailbox (GPU autoscale)
```

The op stream is the only contract: C++ knows nothing about WebGPU, JS
knows nothing about matplotlib styling.

## Building from source

```bash
# requires emsdk on PATH
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DVOLCANO_WEB=ON
emmake cmake --build build-web -j4
cd web && npm install && npm run build
```

## Testing

```bash
cd web
npx tsc -p tsconfig.json     # typecheck
npx vitest run               # unit tests (op-stream decoding)
npx playwright test          # real-browser pixel tests (SwiftShader)
```
