// volcano/render/Offload.hpp — opt-in GPU offload policy
//
// Every "could run on the GPU" workload has a CPU implementation and a
// GPU implementation. Some are clear wins (contour extraction, polyline
// stroking at large N); others are nominally faster on the CPU but a
// system integrator may still want them on the GPU to keep the CPU
// free — notably on the WebGPU/WASM backend, where the whole engine
// runs on one main thread and a busy CPU blocks input and animation.
//
// `OffloadMode::Auto`  — use the GPU path when the backend implements it.
// `OffloadMode::Gpu`   — prefer the GPU path; fall back only if the
//                        backend cannot do it at all.
// `OffloadMode::Cpu`   — never use the GPU path for this workload.
//
// Native processes configure this once from the environment:
//   VOLCANO_GPU_OFFLOAD=binning=gpu,contours=cpu,fft=gpu
//   (VOLCANO_GPU_HIST=1 is still honoured as binning=gpu)
// Browser builds configure it from JS: vp.setOffload({...}) — see
// web/src/volcano.ts.
#pragma once

#include <string_view>

namespace volcano::render {

enum class OffloadMode { Auto, Gpu, Cpu };

/// One switch per offloadable workload.
struct OffloadConfig {
    /// Polyline stroking (solid) — `GpuLineRenderer::tessellate`.
    OffloadMode stroking = OffloadMode::Auto;
    /// Dashed polyline stroking (GPU dash expansion).
    OffloadMode dashes = OffloadMode::Auto;
    /// Contour extraction (marching squares / marching triangles).
    OffloadMode contours = OffloadMode::Auto;
    /// 1-D and 2-D histogram binning + hexbin accumulation. Defaults to
    /// the CPU: on native Vulkan the 8-thread host count beats the
    /// upload+dispatch round trip (measured 118 ms GPU vs ~50 ms CPU at
    /// 10M samples). The web build flips this to `Gpu` — WASM is
    /// single-threaded, so the GPU wins there and the CPU stays free.
    OffloadMode binning = OffloadMode::Cpu;
    /// 3-D projection of plot vertices (view-projection in the shader).
    OffloadMode projection3d = OffloadMode::Auto;
    /// Instanced box drawing (voxels, 3-D bars).
    OffloadMode instancing = OffloadMode::Auto;
    /// FFT for the spectrum family (specgram/psd/csd/cohere/spectrum).
    OffloadMode fft = OffloadMode::Auto;
    /// Column envelope reduction over very large point sets.
    OffloadMode envelope = OffloadMode::Auto;
    /// Surface mesh topology pulled in the vertex shader (no CPU
    /// index/vertex expansion for `plot_surface`/`surf`).
    OffloadMode surfacemesh = OffloadMode::Auto;
    /// Quiver/barb arrowhead expansion in the vertex shader.
    OffloadMode arrows = OffloadMode::Auto;
    /// fill_between crossing-point insertion + soup emission.
    OffloadMode fillbetween = OffloadMode::Auto;
    /// Streamline RK4 integration for streamplot.
    OffloadMode streamlines = OffloadMode::Auto;
    /// Painter's-order depth sort for 3-D triangle soup (bitonic).
    OffloadMode depthsort = OffloadMode::Auto;
    /// Density splatting for huge scatter sets (datashader-style —
    /// changes semantics: density replaces overdraw).
    OffloadMode splatting = OffloadMode::Cpu;
    /// O(n·lags) correlation for xcorr/acorr.
    OffloadMode xcorr = OffloadMode::Auto;
    /// Value sort for ecdf (bitonic on the device).
    OffloadMode ecdf = OffloadMode::Auto;
    /// tripcolor per-triangle colormap expansion.
    OffloadMode tripcolor = OffloadMode::Auto;

    /// Process-wide config, initialised from VOLCANO_GPU_OFFLOAD /
    /// VOLCANO_GPU_HIST on first use.
    static OffloadConfig& global();

    /// Apply a `name=mode,name=mode` list. Unknown names are ignored so
    /// callers can pass user input straight through.
    void parse(std::string_view csv);

    /// True when the GPU path may be attempted for `m`
    /// (Auto and Gpu both allow the attempt; the caller still has to
    /// check that the backend implements it).
    [[nodiscard]] static bool allowGpu(OffloadMode m) noexcept {
        return m != OffloadMode::Cpu;
    }
    /// True when the CPU path must be used unconditionally.
    [[nodiscard]] static bool forceCpu(OffloadMode m) noexcept {
        return m == OffloadMode::Cpu;
    }

    /// Name → mode for one setting; returns nullptr when unknown.
    [[nodiscard]] OffloadMode* find(std::string_view name) noexcept;
};

} // namespace volcano::render
