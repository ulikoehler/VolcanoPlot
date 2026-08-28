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
```

## Dependencies (system)

```bash
sudo apt-get install -y libvulkan-dev libsdl3-dev libshaderc-dev glslang-dev \
    spirv-tools vulkan-validationlayers vulkan-utility-libraries-dev \
    libpng-dev libwebp-dev
```

Optional: `libfreetype-dev` (for text rendering).

## Architecture

### Components (modular)

- `volcano_core` — Vulkan device/queue/command abstraction (Vulkan-Hpp + VMA)
- `volcano_backend` — Screen (SDL3) and headless offscreen backends
- `volcano_render` — Render passes, pipelines, MSAA, primitive renderers
- `volcano_plot` — Plot data model, axes, transforms, styles, plot types
- `volcano_encode` — GPU-side image encoding (PNG/WebP via compute) + CPU fallback
- `volcano_text` — SDF glyph atlas text rendering

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

## See Also

- `FEATURES-TODO.md` — matplotlib feature parity tracking
- `docs/` — design documents
