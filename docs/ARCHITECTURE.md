# VolcanoPlot Architecture

## Overview

VolcanoPlot is a Vulkan-based GPU-side plotter for C++23, designed as a
modern C++ reimplementation of the WebGPU VolcanoPlot prototype. It targets
**publication-quality** output with matplotlib-style styling and feature parity.

## Two Operating Modes

### Screen Mode
- SDL3 window with a realtime liveplot
- Vulkan swapchain with MSAA anti-aliasing
- Zoom/pan/infinite-zoom interaction
- GPU-side function evaluation for smooth deep-zoom

### Headless Mode
- Offscreen render to a Vulkan image
- GPU-side readback to CPU buffer
- GPU-side image encoding (PNG/WebP via compute shader)
- Save to file

## Module Dependency Graph

```
volcano_core  (Vulkan-Hpp + VMA: device, queues, commands, buffers, images)
    ↑
volcano_backend  (IBackend: ScreenBackend [SDL3] | HeadlessBackend)
    ↑
volcano_render  (Renderer + primitive renderers: Point, Line, Bar, Pie, Heatmap, Surface, Grid)
    ↑
volcano_plot  (Figure → Axes → IPlot layers: Scatter, Line, Bar, Pie, Heatmap, Volcano, KDE, Function, Surface)
    ↑
volcano_text  (SDF glyph atlas text rendering)
    ↑
volcano_encode  (IImageEncoder: CPU [libpng/libwebp] | GPU [compute shader])
```

## Key Abstractions

### IBackend
Abstract interface implemented by `ScreenBackend` and `HeadlessBackend`.
Both provide:
- `beginFrame()` → returns a begun `vk::CommandBuffer` with render pass active
- `endFrame()` → submits and presents (screen) / resolves (headless)
- `context()` → shared `GpuContext` (instance, device, allocator, command pools)

### IPlot
Abstract interface for plot layers. Each plot type:
- `prepare()` — upload GPU resources (buffers, pipelines)
- `draw()` — record draw commands into the current command buffer
- `contributeToAutoscale()` — extend the viewport for autoscaling

### IImageEncoder
Abstract interface for image encoding:
- CPU implementations: libpng, libwebp, BMP, raw
- GPU implementation: compute-shader PNG filtering (with CPU DEFLATE fallback)

## GPU-Side Techniques (ported from WebGPU prototype)

1. **GPU-side function evaluation** — functions evaluated in compute shaders,
   not on CPU. Sample count proportional to canvas width (infinite zoom).

2. **MSAA anti-aliasing** — hardware multisampling for lines/points. Sample
   count probed at init, fallback to highest supported.

3. **fwidth-based dynamic grid** — grid lines via screen-space derivatives
   in fragment shader. Lines never quantize under zoom.

4. **GPU autoscale** — parallel min/max reduce computes viewport from data,
   read back to CPU asynchronously.

5. **f32 phase decomposition** — for chirp plots at deep zoom, split phase
   into large constant + small delta to avoid f32 quantization.

6. **GPU-side KDE** — stream raw samples to GPU, evaluate kernel density
   estimate into a 2D grid via compute shader.

7. **GPU-side image encoding** — PNG row filtering (None/Sub/Up/Avg/Paeth)
   via compute shader, then CPU-side DEFLATE + PNG container assembly.

## Build System

CMake 3.22+ with modular component structure (mirrors Tether project):
- `cmake/components/*.cmake` — per-component source lists + targets
- `cmake/VolcanoComponent.cmake` — helper for shared+static variants
- FetchContent: Vulkan-Hpp, VMA, GoogleTest
- System deps: Vulkan loader, SDL3, shaderc, libpng, libwebp

## Code Conventions

- C++23, no compiler extensions
- `std::format` over string concatenation
- `std::ranges` algorithms
- `#pragma once` header guards
- Namespace: `volcano::{module}`
- Vulkan-Hpp C++ bindings (`vk::`), not raw C Vulkan API
- VMA for GPU memory allocation
- GoogleTest for tests
- `-j4` max parallelism (limited RAM)
