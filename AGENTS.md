# AGENTS.md - Project Guide for AI Agents

## Project Overview

VolcanoPlot is a Vulkan-based GPU-side plotter for C++23, inspired by the
WebGPU VolcanoPlot prototype. It targets **publication-quality** output with
matplotlib-style styling and feature parity. It runs in two modes:

- **Screen mode** — SDL3 window with a realtime liveplot (zoom/pan/infinite zoom)
- **Headless mode** — offscreen render to a buffer, then GPU-side image encoding
  (PNG/WebP via compute shader) saved to a file

## Build Commands

> **Parallelism limit:** Use `-j4` max. The build machine has limited RAM.

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all
cmake --build build -j4

# Run tests
./build/tests/volcano_tests

# Run screen liveplot demo
./build/bin/volcano_screen

# Run headless export
./build/bin/volcano_headless output.png

# Run headless scatter example (writes headless_scatter.png)
./build/examples/example_headless_scatter
```

## Dependencies (system)

```bash
sudo apt-get install -y libvulkan-dev libsdl3-dev libshaderc-dev glslang-dev \
    spirv-tools vulkan-validationlayers vulkan-utility-libraries-dev \
    libpng-dev libwebp-dev
```

Optional: `libfreetype-dev` and `libharfbuzz-dev` (for text rendering via glyb).

## Architecture

### Components (modular)

- `volcano_core` — Vulkan device/queue/command abstraction (Vulkan-Hpp + VMA)
- `volcano_backend` — Screen (SDL3) and headless offscreen backends
- `volcano_render` — Render passes, pipelines, MSAA, primitive renderers
- `volcano_plot` — Plot data model, axes, transforms, styles, plot types
- `volcano_encode` — GPU-side image encoding (PNG/WebP via compute) + CPU fallback
- `volcano_text` — Bitmap atlas text rendering via glyb (FreeType + HarfBuzz)

### Key Design Patterns

- `IBackend` — abstract backend interface (screen / headless)
- `IPlot` — abstract plot layer interface (scatter, line, bar, ...)
- `IImageEncoder` — abstract encoder interface (CPU / GPU)
- `Figure` → `Axes` → `IPlot` layers (mirrors matplotlib's Figure/Axes model)
- Push constants for per-draw transforms (no UBO overhead for small data)
- Runtime GLSL→SPIR-V compilation via shaderc (with precompiled .spv fallback)

### Code Conventions

- C++23 (`-std=c++23`), no extensions
- `std::format` over string concatenation
- `std::ranges` algorithms where applicable
- `#pragma once` header guards
- Namespace: `volcano::{module}` (e.g. `volcano::core`, `volcano::plot`)
- Vulkan-Hpp C++ bindings (`vk::`), not raw C Vulkan API
- VMA for GPU memory allocation
- GoogleTest for tests

### GPU-Side Techniques (ported from WebGPU prototype)

- **GPU-side function evaluation** — functions evaluated in compute shaders
- **MSAA anti-aliasing** — hardware multisampling for lines/points
- **fwidth-based dynamic grid** — grid lines via screen-space derivatives
- **GPU autoscale** — parallel min/max reduce for viewport computation
- **Infinite zoom** — sample count proportional to canvas width, not viewport
- **f32 phase decomposition** — split phase into large constant + small delta
  to avoid f32 quantization at deep zoom (chirp plots)
- **GPU-side KDE** — stream samples to GPU, evaluate kernel density into grid
- **GPU-side image encoding** — PNG filtering via compute shader

### Text Rendering (glyb Bitmap Atlas)

Text is rendered using the [glyb](https://github.com/larkmjc/glyb) library,
which uses FreeType for glyph rasterization and HarfBuzz for text shaping.
Glyphs are rasterized on-demand into a bitmap font atlas (grayscale, 1024x1024),
uploaded to a Vulkan texture, and rendered as textured quads.

1. **Font loading** — glyb's `font_manager_ft` loads TTF/OTF files via FreeType.
   `findSystemFontFile()` searches common Linux font directories for DejaVu Sans.
2. **Text shaping** — HarfBuzz (`text_shaper_hb`) shapes text segments, producing
   glyph indices and positions (kerning, ligatures, etc.).
3. **Glyph rasterization** — FreeType's `FT_Outline_Render` with span callbacks
   rasterizes each glyph into the font atlas bitmap. This correctly handles
   glyph holes (o, 0, A, etc.) via the even-odd fill rule.
4. **Atlas upload** — `prepareAtlas()` pre-renders all ASCII glyphs and uploads
   the atlas bitmap to a Vulkan R8_UNORM texture using a one-time command buffer
   (outside any render pass).
5. **GPU rendering** — `TextRenderer::draw()` shapes and renders text into a
   `draw_list`, converts glyb vertices to Vulkan vertices (pos, uv, color),
   uploads to scratch VB/IB, and draws as textured quads with alpha blending.

**Key files:**
- `include/volcano/text/TextRenderer.hpp` — GPU pipeline, atlas texture, draw API
- `src/text/TextRenderer.cpp` — glyb integration, vertex conversion, Vulkan draw
- `src/text/glyb_msdf_stub.cpp` — Stub for unused MSDF renderer vtable
- `cmake/components/glyb.cmake` — Builds glyb core as static library
- `dependencies/glyb/` — glyb git submodule (with glm submodule)

**Coordinate mapping:** glyb uses pixel coordinates with Y-down (top-left origin).
The vertex shader converts pixel coords to Vulkan NDC: `ndc = pos / resolution * 2 - 1`.

**Tick computation:** `Renderer::drawText()` uses a nice-number auto-locator
(1/2/5 × 10^k steps) and auto-formatter (%.0f, %.1f, %.2f, %.1e). Tick
labels are rendered below the axes (X) and to the left (Y).

### Depth Attachment and 3D Rendering

Both `HeadlessBackend` and `ScreenBackend` now create a depth attachment
(`vk::Format::eD32Sfloat` or fallback) alongside the color attachment.
The render pass clears depth to 1.0 and uses `eDepthStencilAttachmentOptimal`
layout. All 2D overlay pipelines (Point, Line, Bar, Pie, Heatmap, Grid, Text,
Spine) have depth testing disabled. The `SurfaceRenderer` pipeline has depth
testing and writing enabled (`eLess` compare), enabling correct 3D surface
occlusion.

**Key additions:**
- `backend::findDepthFormat()` — picks a supported depth format
- `IBackend::depthFormat()` — exposes the chosen format
- All pipelines set `pDepthStencilState` (required when render pass has depth)

### Axis Spines, Legend, and Colorbar

**SpineRenderer** (`src/render/primitives/SpineRenderer.cpp`) draws:
- Axis border rectangles (line strip pipeline)
- Filled rectangles for legend backgrounds and markers (triangle list pipeline)
- Tick marks along axes

Both pipelines use a shared host-visible scratch vertex buffer (ring-buffered,
reset per frame via `resetScratch()`).

**Legend rendering** (`Renderer::drawLegend()`):
- Collects labels and colors from all `IPlot` layers via `label()` and `legendColor()`
- Draws a semi-transparent background box, border, colored marker squares, and text labels
- Positioned in the upper-right of the axes rect by default
- Controlled by `LegendStyle::visible`

**Colorbar rendering** (`Renderer::drawColorbar()`):
- Draws a vertical color strip to the right of the axes rect
- Samples colors from a named colormap (e.g., "viridis") across 64 segments
- Draws tick labels along the strip
- Uses the z-axis viewport range for value mapping
- Controlled by `ColorbarStyle::visible`

## Regression Test System

The project includes a pixel-level regression test system that renders plots
headlessly and verifies the output framebuffer. Tests are in
`tests/test_render_regression.cpp` with helpers in `tests/PlotTestHarness.hpp`.

### Running regression tests

```bash
# Run all tests (including regression)
./build/tests/volcano_tests

# Run only render regression tests
./build/tests/volcano_tests --gtest_filter='*Regression.*'

# Run a specific test suite
./build/tests/volcano_tests --gtest_filter='ScatterRegression.*'
```

On failure, test images are saved to `/tmp/volcano_test_<Suite>_<Test>.png`
for manual inspection.

### Test harness API

`PlotTestHarness` (in `tests/PlotTestHarness.hpp`) provides:

- **`Image`** — RGBA8 pixel buffer with analysis methods:
  - `countColor(pixel, tolerance)` — count matching pixels
  - `countColorInRegion(...)` — count in a sub-rectangle
  - `boundingBox(pixel, tolerance)` — find bbox of matching pixels
  - `centroid(pixel, tolerance)` — mean (x,y) of matching pixels
  - `averageRegion(...)` — average color in a region
  - `countIf(predicate)` — count with custom predicate
  - `save(path)` — save as PNG (or PPM fallback)
- **`PlotTestHarness`** — renders a `Figure` headlessly and returns an `Image`
- **GTest macros**: `EXPECT_PIXEL_AT`, `EXPECT_PIXEL_COUNT`,
  `EXPECT_REGION_UNIFORM`, `EXPECT_FULLY_OPAQUE`

### Crafted-plot design strategies

Tests use **crafted plots** designed for deterministic verification:

1. **Flat background, no grid, no axes** — `flatTestStyle()` produces a
   constant white background so background pixels are predictable. Grid lines
   and axes would interfere with pixel assertions.

2. **Small canvas (64–256px)** — each pixel is meaningful, tests run fast
   (~0.8s per test including Vulkan init).

3. **No MSAA for pixel-exact tests** — `vk::SampleCountFlagBits::e1` makes
   pixel assertions deterministic. MSAA is tested separately in
   `MSAaRegression` which checks for anti-aliased edge pixels.

4. **Saturated primary colors** — pure red `(255,0,0)`, green `(0,255,0)`,
   blue `(0,0,255)` with tolerance ~40 for robust color matching against
   blending and rasterization differences.

5. **Known data coordinates** — points placed at `(0,0)`, `(0.5,0.5)`,
   `(1,1)` etc. so expected pixel positions can be computed analytically.
   The renderer uses **Y-up math convention** (flips Y in the vertex shader),
   so data `(x, y)` maps to pixel `(W*x, H*(1-y))`.

6. **Centroid-based assertions** — for points at canvas corners, the centroid
   is shifted inward because only the visible portion of the marker
   contributes. Use wider tolerances (±25–30px) for corner points.

7. **Bounding-box assertions** — for lines, check that the bbox spans the
   expected canvas extent (e.g., horizontal line should span x=[0, 255]).

8. **Area-ratio assertions** — for pie charts, check that slice areas are
   roughly proportional to their values (within 2× ratio for 50/50 splits).

9. **Uniformity assertions** — for heatmaps with uniform data, check that
   the center region has a single constant color (within tolerance 20).

### Bugs found by regression tests

The regression test system has found and verified fixes for:

- **Blend state alpha overwrite** — all blended renderers replaced the
  framebuffer alpha with source alpha, causing transparent pixels in the
  output PNG. Fixed by setting `srcAlphaBlendFactor=eZero, dstAlphaBlendFactor=eOne`.
- **Staging buffer missing eTransferDst** — `BufferUsage::Staging` only had
  `eTransferSrc`, so `copyImageToBuffer` (readback) silently did nothing.
- **OneTimeCommands race** — readback copied mapped memory before the
  copy command was submitted (destructor ran too late).
- **LineRenderer missing color/width** — `upload()` ignored color and width
  parameters; push constant struct didn't set them in `draw()`.
- **LineRenderer push constant layout mismatch** — GLSL std140 padding
  between `vec2` and `vec4` didn't match the C++ struct layout. Fixed by
  rearranging fields to put all `vec4`s before `vec2`/`float`.
- **LineRenderer lineWidth=0** — `vk::PipelineRasterizationStateCreateInfo`
  defaults `lineWidth` to 0.0f (not 1.0f); all renderers needed explicit
  `setLineWidth(1.0f)`.
- **PieRenderer NDC calculation** — `ndc = center/halfExtent + a_pos` shifted
  the pie off-screen. Fixed to `ndc = a_pos` (viewport handles pixel mapping).
- **HeatmapRenderer grid range mixing** — shader mixed x and y ranges
  (`u_gridRange.xy` was `(xMin, xMax)` but used as `(xMin, yMin)`). Fixed
  to use explicit `.x`, `.y`, `.z`, `.w` components.
- **HeatmapRenderer quad upload** — `std::span{kQuad, 6}` uploaded 6 floats
  (3 vertices) instead of 12 floats (6 vertices), so only one triangle of
  the fullscreen quad rendered.

## See Also

- `FEATURES-TODO.md` — matplotlib feature parity tracking
- `docs/` — design documents
