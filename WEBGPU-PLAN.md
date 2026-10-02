# WEBGPU-PLAN.md — VolcanoPlot on WebGPU (implementation spec)

**Decision record:** Option A chosen — the *draw-op stream* architecture.
C++ plot engine compiled to WASM; TypeScript side interprets a serialized op
buffer into WebGPU calls. Rejected: full-HAL `webgpu.h` refactor (async/GC
impedance inside C++, bigger blast radius), pure-TS rewrite (permanent fork),
Lavapipe-in-WASM (CPU speed), server-side rendering (not a library).

This document is the implementation spec. It is written so that each section
can be executed independently once Phase 0 lands. Where something is a choice,
the chosen option is marked **[FIXED]** — do not re-decide it.

---

## 0. Architecture

```
┌─────────────────────────── WASM (Emscripten) ───────────────────────────┐
│ volcano_plot (all logic)   volcano_text layout (glyb/FreeType/HarfBuzz) │
│ Renderer orchestration     VectorRenderer/Writers (SVG/PDF export)      │
│ OpStream impls of I* primitives → frame op buffer                       │
│ embind facade (handles + typed arrays + JSON kwargs)                    │
└──────────────┬───────────────────────────────────────────┬──────────────┘
               │ one op buffer per frame                   │ value calls
┌──────────────▼───────────────────────────────────────────▼─────────────┐
│ @volcanoplot/web (TypeScript, ESM)                                      │
│  OpReader → Interpreter → ~16 WGSL pipelines + resource table           │
│  CanvasBackend (surface/MSAA/depth/DPR/events)   WasmTextAtlas          │
└────────────────────────────────────────────────────────────────────────┘
```

**The seam:** `IPlot::draw(vk::CommandBuffer, …)` becomes
`IPlot::draw(render::Cmd&, …)`. Every primitive renderer gets an abstract
interface `I*Renderer`; plots hold `std::unique_ptr<I*Renderer>` obtained from
`render::Renderer::gpu()` (a `GpuServices` factory/registry). Native impls
wrap today's classes verbatim; web impls record ops.

---

## 1. Phase 0 — the command seam (native refactor; no web code)

Goal: `src/plot/**` and `src/render/Renderer.cpp` compile with **zero**
`vk::` types in signatures. Gate: `./build/tests/volcano_tests` fully green.

### 1.1 New core types

Create `include/volcano/render/Cmd.hpp`:

```cpp
#pragma once
namespace volcano::render {
/// Opaque per-frame command context passed through draw paths.
/// Vulkan build: VkCmd { vk::CommandBuffer cmd; }.  Web build:
/// web::OpCmd { OpStream& ops; }.  Impls downcast per backend.
class Cmd { public: virtual ~Cmd() = default; };
/// Opaque GPU resource token. Backend-defined meaning:
/// Vulkan impls pack the VkBuffer/VkImage; web impls pack a u32 handle.
using GpuBuf = uint64_t;
using GpuTex = uint64_t;
}
```

Create `include/volcano/render/GpuServices.hpp` — the factory interface:

```cpp
class GpuServices {
public:
    virtual ~GpuServices() = default;
    // factory methods — plots own the returned objects
    virtual std::unique_ptr<IPointRenderer>    createPointRenderer() = 0;
    virtual std::unique_ptr<ILineRenderer>     createLineRenderer() = 0;
    virtual std::unique_ptr<ILineSegRenderer>  createLineSegRenderer() = 0;
    virtual std::unique_ptr<IFillRenderer>     createFillRenderer() = 0;
    virtual std::unique_ptr<IBarRenderer>      createBarRenderer() = 0;
    virtual std::unique_ptr<IPieRenderer>      createPieRenderer() = 0;
    virtual std::unique_ptr<IHeatmapRenderer>  createHeatmapRenderer() = 0;
    virtual std::unique_ptr<ISurfaceRenderer>  createSurfaceRenderer() = 0;
    virtual std::unique_ptr<IEvalRenderer>     createEvalRenderer() = 0;
    virtual std::unique_ptr<IKdeEvalRenderer>  createKdeEvalRenderer() = 0;
    virtual std::unique_ptr<IHistBinsRenderer> createHistBinsRenderer() = 0;   // §4.16
    virtual std::unique_ptr<IPcmTessRenderer>  createPcmTessRenderer() = 0;    // §4.17
    virtual std::unique_ptr<IViolinKdeRenderer> createViolinKdeRenderer() = 0; // §4.18
    // shared renderers owned by the services object
    virtual ISpineRenderer&         spine() = 0;
    virtual IInstancedPathRenderer& instancedPath() = 0;
    virtual IGpuLineRenderer&       gpuLine() = 0;
    virtual IReduceRenderer&        reduce() = 0;
    virtual ITextRenderer&          text() = 0;
    // frame info (replaces r.backend().extent()/sampleCount() reads in plots)
    virtual plot::Extent2D extent() const = 0;
    virtual uint32_t sampleCount() const = 0;
    /// The op-stream handle allocator / nothing on Vulkan. Web impls use it.
    virtual OpSink* opSink() { return nullptr; }   // §2
};
```

### 1.2 Interface extraction — per primitive

For each renderer below, create `include/volcano/render/primitives/I<X>.hpp`
with the same public methods minus `vk::` types. Mechanical rules:

- `vk::CommandBuffer` → `render::Cmd&` (first param, same position)
- `vk::Rect2D` → `plot::Rect2D` (same fields: `{i32 x,y; u32 w,h}`)
- `vk::Extent2D` → `plot::Extent2D`
- `vk::Buffer` return/params → `GpuBuf`
- `init(device, renderPass, samples, descPool, cache)` → `init(GpuServices&)`
- `upload(device, queue, pool, allocator, …)` → `upload(…)` (data spans only)

| Primitive (file) | Interface methods (signatures after refactor) |
|---|---|
| PointRenderer | `init(GpuServices&)` · `upload(span<Point2D>, span<Color>, span<float>)` · `updatePoints(same)` · `draw(Cmd&, Rect2D viewport, Rect2D scissor, const Transform2D&, u32 count, MarkerParams)` · `pointBuffer()→GpuBuf` · `pointCount()→u32` · `hasData()→bool` |
| LineRenderer | `init(GpuServices&)` · `upload(span<Point2D>, Color, float width)` · `updatePoints(span<Point2D>)` · `bindExternalBuffer(GpuBuf, u32)` · `draw(Cmd&, Rect2D, const Transform2D&, u32)` · `pointBuffer()→GpuBuf` |
| LineSegmentRenderer | same shape; `draw(Cmd&, Rect2D, const Transform2D&, u32 vertexCount)` |
| FillRenderer | `init` · `upload(span<Point2D>, span<Color>)` · `adoptBuffers(GpuBuf pos, GpuBuf col, u32)` · `draw(Cmd&, Rect2D, const Transform2D&)` · `pointBuffer/pointCount` |
| BarRenderer | `init` · `upload(span<Point2D>, span<Color>)` · `draw(Cmd&, Rect2D, const Transform2D&)` |
| PieRenderer | `init` · `upload(span<PieVert>)` · `draw(Cmd&, Rect2D)` (pie-space transform stays in uniforms — see §4.7) |
| HeatmapRenderer | `init` · `upload(u32 w, u32 h, span<float> grid \| span<u8> rgba, bool originLower, bool rgbaMode, bool nanTransparent, TexParams)` · `draw(Cmd&, Rect2D, const Transform2D&)` |
| SurfaceRenderer | `init` · `upload(span<Vert3D> verts, span<u32> idx)` · `draw(Cmd&, Rect2D, const Camera3D&, bool shade)` |
| InstancedPathRenderer | `init` · `setTemplate(span<Point2D> triVerts)` · `drawInstanced(Cmd&, Rect2D clip, const Transform2D&, span<PathInstance>)` · `resetScratch()` |
| SpineRenderer (pixel-space) | `init` · `drawRect` · `drawFilledRect` · `drawTicks` · `drawLineStrip(Cmd&, Rect2D clip, Extent2D res, span<Point2D>, Color, float w)` · `drawTriangles(…)` · `drawTrianglesVC(…)` · `drawTrianglesGpu(Cmd&, Rect2D clip, Extent2D res, GpuBuf, u64 byteOffset, u32 count)` · `resetScratch()` |
| GpuLineRenderer | `init` · `tessellate(Cmd&, span<Point2D> px, StrokeParams, Color) → vector<Mesh>` where `Mesh{GpuBuf buffer; u32 byteOffset; u32 vertexCount}` · `envelopeColumns(…)→bool+out vectors` (Phase-2 mailbox) · `resetScratch()` |
| ReduceRenderer | `init` · `reduceMinMax2D(GpuBuf, u32 count) → optional<MinMax2D>` |
| EvalRenderer | `init` · `compile(string body)→bool` · `eval(GpuBuf out, double xMin, double xMax, u32 count)` · `makeOutput(u32 count)→GpuBuf` (+ allocation helper on the services, see §1.4) · `ready/compiled` |
| KdeEvalRenderer | `init` · `eval(span<Point2D> samples, u32 w, u32 h, KdeParams) → vector<float>` (async-mailbox path on web, §6) |
| TextRenderer | `init` · `initFonts` · `setFontData(span<u8> ttf)` **new** · `prepareAtlas` · `draw(Cmd&, Rect2D clip, string_view, x, y, Color, scale, rot)` · `measure(…)` — layout API is backend-free already; impl lives in WASM on web |
| Grid3DRenderer | `init` · `draw(Cmd&, Rect2D, const Camera3D&, Viewport view3d, Color grid, float step)` |

`IPlot` signature changes (`include/volcano/plot/Plot.hpp`):

```cpp
virtual void preDraw(render::Cmd& cmd, render::Renderer& r,
                     const Axes&, Rect2D) { … }
virtual void draw(render::Cmd& cmd, render::Renderer& r,
                  const Axes&, Rect2D) = 0;
virtual void contributeToAutoscaleGpu(render::IReduceRenderer&, Viewport&) const;
[[nodiscard]] Rect2D clipRect(Rect2D axesRect, Extent2D canvas) const; // was clipRectVk
```

### 1.3 Mechanical refactor recipe (apply to all ~90 plot files)

Per file:
1. `grep -n 'vk::'` — every hit is one of:
   - `vk::CommandBuffer` → `render::Cmd&`
   - `vk::Rect2D{vk::Offset2D{a.x,a.y}, vk::Extent2D{a.w,a.h}}` → `Rect2D{a.x, a.y, a.w, a.h}` (plot::Rect2D, same field order)
   - `vk::Extent2D` → `Extent2D`
   - `r.backend().extent()` → `r.gpu().extent()`
   - `r.spineRenderer()` → `r.gpu().spine()` etc. for all shared accessors
   - member `render::primitives::PointRenderer renderer_;` →
     `std::unique_ptr<render::primitives::IPointRenderer> renderer_;`
2. `prepare()`: replace `renderer_.init(ctx.device.handle(), renderPass, …)`
   + `renderer_.upload(device, queue, pool, allocator, data…)` with:
   ```cpp
   if (!renderer_) renderer_ = r.gpu().createPointRenderer();
   if (!prepared_) { renderer_->init(r.gpu()); renderer_->upload(pts, cols, sz); }
   ```
   Update paths keep calling `renderer_->updatePoints(…)` unchanged.
3. `draw()`: `renderer_.draw(cmd, vrect, t, n, mp)` →
   `renderer_->draw(cmd, vrect, vrect /*scissor*/, t, n, mp)` (the two-rect
   overload already exists on PointRenderer — hoist clip/viewport split into
   the interface everywhere).
4. Plots with **own pipelines** (HistPlot, ViolinPlot, PcolormeshPlot,
   FunctionPlot eval path): move the embedded compute shader + dispatch into
   `src/render/primitives/` as a new primitive (`HistBinsRenderer`,
   `ViolinKdeRenderer`, `PcmTessRenderer`) implementing the interface above —
   this also removes a layering violation that exists today. FunctionPlot
   keeps `IEvalRenderer` + `ILineRenderer` (it already composes those two).

### 1.4 Vulkan side after refactor

- `class VulkanGpuServices : public GpuServices` in
  `src/render/VulkanGpuServices.cpp` owns the concrete shared renderers
  (today's `Renderer` members move here) and vends `unique_ptr`s of the
  existing concrete classes.
- `class VkCmd : public Cmd { vk::CommandBuffer h; }` — Vulkan primitive
  impls do `auto c = static_cast<VkCmd&>(cmd).h;` at method entry (one line,
  then the existing body runs verbatim).
- `IBackend::beginFrame()` returns `std::unique_ptr<Cmd>` — ScreenBackend/
  HeadlessBackend wrap their begun `vk::CommandBuffer` in `VkCmd`.
- `GpuBuf` on Vulkan = `reinterpret_cast<uint64_t>(VkBuffer)`; `GpuTex` =
  `VkImage` likewise (pack/unpack helpers in `VulkanGpuServices`). Documented
  as backend-defined.
- `Renderer` keeps its orchestration methods unchanged; its concrete renderer
  members are replaced by `gpu_` accessors — `textRenderer_`, `spineRenderer_`
  etc. live on `VulkanGpuServices`; `Renderer::textRenderer()` forwards.
- `GpuServices` needs one buffer-allocator method for EvalRenderer's
  `makeOutput` and plot-owned scratch: `createStorageBuffer(u64 bytes) → GpuBuf`
  + `releaseBuffer(GpuBuf)` + `writeBuffer(GpuBuf, offset, span<byte>)`.

### 1.5 Acceptance

- [ ] `cmake --build build -j4` clean; `./build/tests/volcano_tests` 100% green
- [ ] `grep -rn 'vk::' src/plot include/volcano/plot` → only inside
      `#ifdef` or comment remnants (target: zero)
- [ ] `grep -rn 'vulkan' include/volcano/plot` → zero
- [ ] benchmarks `examples/bench_line`, `bench_matrix` within ±2% of pre-refactor

---

## 2. Op stream format (the wire protocol)

A frame produces **one contiguous byte region** in WASM memory:
`[header][op records][arena]`. `renderFrame()` returns `(ptr, len)`.

```
Header (32 B):
  u32 magic = 'VPOP' (0x564F5050)   u16 version = 1    u16 flags
  u64 frameSeq                      u32 canvasW        u32 canvasH
  u32 opCount                       u32 arenaOffset    (abs. offset into region)
  u32 clearColor RGBA8 packed       u32 loadOp (0=clear,1=load)
Op record:
  u16 opcode   u16 flags   u32 payloadBytes   payload…   (pad to 4 B)
Payload references to bulk data: struct BufSrc { u8 kind; u8 pad[7];
  u64 off; u64 len; }  kind: 0=ARENA (off = offset into region arena),
  1=HEAP (off = absolute WASM memory address of caller-stable storage).
```

**Arena rule [FIXED]:** v1 copies *all* span payloads into the arena
(`OpStream::arenaCopy(span)`). `HEAP` is a later optimization for
`createBuffer`/`writeBuffer` from series storage, which outlives the frame.
Never reference C++ temporaries — they die before the interpreter runs.

**Handles [FIXED]:** `u32`, monotonically allocated by `OpStream::allocHandle()`
per figure session; never reused within a session. `Release*` ops are deferred
by the interpreter until after frame submission (mirrors `retired_` semantics).

### Op table (v1 complete set)

| # | Op | Payload | Emitted by |
|---|---|---|---|
| 1 | `CreateBuffer` | `u32 handle, u64 size, u8 kind(0=vertex,1=storage,2=uniform,3=mapread)` | resource impls |
| 2 | `WriteBuffer` | `u32 handle, u64 dstOff, BufSrc data` | resource impls, streaming |
| 3 | `ReleaseBuffer` | `u32 handle` | resource impls |
| 4 | `CreateTexture` | `u32 handle, u32 w, u32 h, u8 fmt(0=r8,1=rgba8,2=r32f)` | atlas, heatmap, image plots |
| 5 | `WriteTexture` | `u32 handle, u32 x,y,w,h, BufSrc rgba/r32 data` | atlas updates, heatmap upload |
| 6 | `ReleaseTexture` | `u32 handle` | |
| 10 | `DrawTrisPx` | `Rect2Df clip, BufSrc verts(f32 x,y), Color` | spine tris, fills built per-frame |
| 11 | `DrawTrisPxVC` | `clip, BufSrc verts, BufSrc colors` | vertex-colored fills |
| 12 | `DrawLineStripPx` | `clip, BufSrc pts, Color, f32 widthPx` | grid, spines, markers (strokes) |
| 13 | `DrawSegmentsPx` | same as 12 | tick marks, error bars, broken_barh edges |
| 14 | `DrawTextQuads` | `clip, u32 atlasTex, BufSrc quads(f32 x,y,u,v, rgba f32×4 → packed)` | TextRenderer |
| 15 | `DrawInstanced` | `clip, BufSrc templateVerts, BufSrc instances(PathInstance), TransformUBO` | InstancedPath |
| 20 | `DrawLines` | `clip, Rect2Df viewRect, TransformUBO, u32 buf, u32 count, Color, f32 width` | LineRenderer |
| 21 | `DrawLineSegs` | same as 20 | LineSegmentRenderer |
| 22 | `DrawPoints` | `clip, Rect2Df viewRect, TransformUBO, u32 posBuf, u32 colBuf, u32 sizeBuf, u32 count, MarkerParams` | PointRenderer |
| 23 | `DrawTrisData` | `clip, TransformUBO, u32 posBuf, u32 colBuf (0=uniform), u32 vertCount` | FillRenderer, BarRenderer, pcm output |
| 24 | `DrawTrisGpu` | `clip, Extent2D res, u32 buf, u64 byteOff, u32 count` | spine.drawTrianglesGpu (GPU-tessellated mesh) |
| 25 | `DrawPie` | `clip, u32 buf, u32 count, PieUBO(center, ndcScale)` | PieRenderer |
| 26 | `DrawImage` | `Rect2Df viewRect, TransformUBO, u32 gridTex, u32 cmapTex, ImgParams(interp,rgbaMode,nanSkip,valueRange,gridRange)` | HeatmapRenderer/FigImage |
| 27 | `DrawSurface` | `Rect2Df viewRect, u32 vbuf, u32 ibuf, u32 indexCount, SurfaceUBO(vp mat4, ranges, light, shade)` | SurfaceRenderer/wireframe/3D |
| 28 | `DrawGrid3D` | `Rect2Df, Grid3DUBO(invVP rows, viewXYZ, flags, color, step)` | Grid3DRenderer |
| 40 | `TessLines` | `u32 inBuf, u32 inBase, u32 outBuf, u32 outBase, u32 n, u32 nSeg, f32 hwidth, u8 join, u8 cap, f32 miterLimit, Color` | GpuLineRenderer::tessellate |
| 41 | `EvalFunc` | `u32 outBuf, f64 xMin, f64 xMax, u32 count, u16 funcId` | EvalRenderer |
| 42 | `FuncDef` | `u16 funcId, u8 lang(0=glsl-expr,1=wgsl-expr), BufSrc bodyString` | EvalRenderer::compile — defines id↔shader mapping |
| 43 | `ReduceMinMax` | `u32 inBuf, u32 count, u32 mailbox` | ReduceRenderer (async, §6) |
| 44 | `KdeEval2D` | `u32 samplesBuf, u32 n, u32 outTexOrBuf, u32 w,h, KdeParams, u32 mailbox` | KdeEvalRenderer |
| 45 | `HistBins` | `u32 srcBuf, u32 n, u32 binsBuf, HistParams, u32 mailbox` | HistPlot |
| 46 | `PcmTess` | `u32 xs,ys,ts,lut, posOut,colOut, u32 nCols,nRows, u8 gouraud, u8 flags` | PcolormeshPlot |
| 47 | `ViolinKde` | `u32 srcBuf, u32 n, u32 dstBuf, Kde1DParams, u32 mailbox` | ViolinPlot |
| 60 | `Mailbox` (internal convention) | mailbox = index into a fixed 64-slot f32×4 array in WASM; interpreter writes results via `writeBuffer` to a mailbox staging buffer, then `mapAsync`→`Module._vp_mailbox(i, v0..v3)` | — |

`TransformUBO` = the 6-vec4 push block used today (`u_viewMinSpan, u_rect,
u_scaleX, u_scaleY, u_proj` + per-op extras like `u_color/u_marker/u_width`).
The uniform layout per op is exactly the push-constant struct of the source
renderer — copy field order verbatim (§4).

**Ordering [FIXED]:** record order = execution order. The interpreter
partitions the stream: resource ops (1–6) execute during the walk; compute
ops (40–47) record into the compute pass; draw ops (10–28) record into the
render pass. This matches today's `preCmd_` submit-before-frame semantics.

---

## 3. WASM-side implementation (`src/web/`)

New files (all compile only under `VOLCANO_WEB` / Emscripten):

| File | Contents |
|---|---|
| `src/web/OpStream.hpp/.cpp` | `OpStream` class: `emit(op, payload)`, `arenaCopy(span)→BufSrc`, `allocHandle()`, `finish()→{ptr,len}`. Owns `std::vector<std::byte> buf_, arena_`; `finish` concatenates. |
| `src/web/OpCmd.hpp` | `class OpCmd : public render::Cmd { OpStream* s; }` |
| `src/web/OpPrimitives.cpp` | ~15 recording impls of the `I*` interfaces. Pattern per method: e.g. `OpPointRenderer::upload` → `CreateBuffer`×3 + `WriteBuffer`×3, stores handles; `draw` → `DrawPoints` op. |
| `src/web/OpGpuServices.cpp` | `GpuServices` impl: allocates OpStream, owns shared Op* singletons, `extent()` returns canvas size pushed from JS. |
| `src/web/WebBackend.cpp` | `IBackend` impl: `beginFrame` → allocates `OpCmd` + writes header fields; `endFrame` → `finish()`; `readbackRgba8` → empty (canvas owns pixels; export path is JS-side). |
| `src/web/mailbox.cpp` | `extern "C" void vp_mailbox(u32 idx, f32 a,b,c,d)` — writes the fixed mailbox array + marks owning figure stale. |
| `src/web/api.cpp` | embind facade (§5). |

Recording-impl edge cases (write these explicitly):

- `OpLineRenderer::bindExternalBuffer(buf, count)` stores the foreign
  handle; `draw` emits `DrawLines` with it — this is how `EvalFunc` output
  reaches the line pipeline (GPU→GPU, no readback) — same as native.
- `OpGpuLineRenderer::tessellate` emits `TessLines` + returns
  `Mesh{outHandle, offset, count}` descriptors; the follow-up
  `spine().drawTrianglesGpu(…)` emits `DrawTrisGpu` referencing them.
- `OpReduceRenderer::reduceMinMax2D` emits `ReduceMinMax{mailbox=k}` and
  returns `std::nullopt` — the existing default
  `IPlot::contributeToAutoscaleGpu` then falls back to CPU autoscale
  automatically. GPU autoscale arrives a frame later once §6 lands; v1 ships
  correct-but-CPU autoscale.
- `OpKdeEvalRenderer::eval` emits `KdeEval2D` with a mailbox, returns `{}`
  first frame. Plots already tolerate empty results (guard exists upstream:
  `if (!r) CPU-fallback` patterns). **Verify per call site; where no
  fallback exists, keep CPU evaluation on web via a `VOLCANO_WEB` branch.**
- `OpTextRenderer`: glyb runs in WASM (rasterize CPU-side as today, using
  `setFontData`-loaded TTFs). `draw` emits, per text call:
  `WriteTexture`(atlas dirty region, only when atlasDirty) + `DrawTextQuads`
  (the quad span glyb produces — same vertex layout as the Vulkan text path:
  `{pos.xy, uv.xy, rgba}`).
- `OpSpineRenderer`: immediate-mode spans → `arenaCopy` into ops
  10–13. `drawTrianglesGpu` → op 24.

---

## 4. Shader port catalog — every shader, with porting notes

Shared chunk first — port once, import everywhere:

**`shaders/transform.wgsl`** ← `src/render/shaders/TransformGlsl.hpp`
Functions `scaleFwd`, `projFwd`, `scaleInv`, `projInv` — direct
transliteration with the cookbook in §7. Add `struct TransformPC {…}` as the
uniform struct. Every data-space shader declares
`@group(0) @binding(0) var<uniform> pc : TransformPC;` — the interpreter
binds a 256 B-aligned slot of the uniform ring.

| # | WGSL file | From (GLSL source) | Kind | Port notes |
|---|---|---|---|---|
| S1 | `px_tri.wgsl` | SpineRenderer.cpp:18-44 | vert+frag | Pixel-space pos→NDC `pos/res*2-1`, keep `y` NOT flipped (vertex already Y-down px; native relies on viewport). Per-vertex color in, flat out. Uniform: `{res.xy, lineWidth,pad}` → vec4. |
| S2 | `px_tri_vc.wgsl` | same file, tri-list variant | vert+frag | Same as S1; separate pipeline object (topology `triangle-list`). |
| S3 | `px_line.wgsl` | same | vert+frag | **No wide lines in WebGPU.** Interpreter-side: `DrawLineStripPx`/`DrawSegmentsPx` expand each segment to a 6-vert quad in JS before upload (pixel space, trivial extrusion `±perp(d)·w/2`, butt ends — matches GPU line rasterization: no joins between segments). Shader then = S2. For width==1 this still works — do NOT use `line-list` topology at all (uniform path). |
| S4 | `data_tri.wgsl` | FillRenderer/BarRenderer vert+frag | vert+frag | `a_pos` data coords → `scaleFwd`/`projFwd` → NDC (copy formula verbatim incl. `-ndc.y`). Per-vertex color + uniform-color variant via `colBuf==0` → use `pc.u_color`. |
| S5 | `data_line.wgsl` | LineRenderer/LineSegmentRenderer | vert+frag | **Vertex-pulling tessellation** (replaces `eLineStrip`+`setLineWidth`): storage buffer `pts: array<vec2f>`; vertex shader computes `seg = vi/6, corner = vi%6`, reads `pts[seg]`,`pts[seg+1]`, applies scaleFwd/projFwd to both, extrudes ±perp·w/2 *in screen space after transform* (uniform carries `resolution`), emits both corner verts + color. Handles `DrawLines` and `DrawLineSegs` (segment stride: `seg=vi/6` pairs → `pts[2*seg]`,`pts[2*seg+1]` via `flags` field). Ring-buffer variant (§8) adds modulo addressing — keep same file with `#ifdef`-style source assembly. |
| S6 | `points.wgsl` | PointRenderer.cpp:21-224 | vert+frag | **Point sprites → instanced quads** (no `gl_PointSize`): one instance per point; `quadCorner[6]` const; storage buffers pos/color/size (or 3 vertex-buffer streams — simpler: vertex buffers at locations 0-2 instanced). Vertex: expand corner·size/2 in px post-transform; compute `uv=corner` → frag. Frag: port `markerDist` switch **verbatim** incl. all helper SDFs; `c = uv` replaces `gl_PointCoord*2-1`; `isnan`→`x!=x`; `mod`→`gmod()` helper (§7). Uniform = 24-float block. |
| S7 | `pie.wgsl` | PieRenderer.cpp:15-46 | vert+frag | Pie-units→NDC: `ndc = (a_pos + u_center) * u_ndcScale`, `-ndc.y`. Uniform 4 floats. |
| S8 | `instanced.wgsl` | InstancedPathRenderer.cpp:12-45 | vert+frag | Template verts via binding-0 VB; instances via binding-1 VB (`offset,size,color`); px→NDC. Trivial. |
| S9 | `text_quads.wgsl` | TextRenderer.cpp:46-84 | vert+frag | Glyph quads + atlas texture. Two variants behind `flags`: gray R8 (`alpha = sample.r`) and MSDF (`median3` + `fwidth` — exists in WGSL). `texture(u,v)` → `textureSample(atlas, smp, v_uv)`. |
| S10 | `heatmap.wgsl` | HeatmapRenderer.cpp:17-160 | vert+frag | Grid texture (r32float `texture_2d<f32>` unfilterable → `textureLoad` + manual bilinear; or convert to rgba8 LUT — **choose: keep r32f + manual bilinear/bicubic, matching native math exactly**). Port `crWeights`, `bicubicSample`, inverse-projection path (uses `projInv`/`scaleInv` — include transform.wgsl). `textureSize`→`textureDimensions`, `texelFetch`→`textureLoad`. Cmap LUT = second texture rgba8. |
| S11 | `surface3d.wgsl` | SurfaceRenderer.cpp:13-90 | vert+frag | `mat4 u_vp` in uniform (mat4x4f, same col-major layout — memcpy-compatible); viridis LUT → replace inline if-chain with 256×rgba8 LUT texture (parity with colormap system anyway) **or** port the if-chain verbatim — choose LUT (simpler). Depth: `depth-compare: less`, `depth-write: true`. |
| S12 | `grid3d.wgsl` | Grid3DRenderer.cpp:13-… | vert+frag | Fullscreen triangle; inverse-VP ray → 3 plane intersection + `fwidth` AA grid. Uniform = ~14 vec4s. Mechanical. |
| C1 | `line_tess.wgsl` | GpuLineRenderer.cpp:30-140 | compute 256 | Polyline→mesh: segment quads + join/cap arc fans. Port `perp/emitVert/emitTri/zeroSlot/arcFan/finitePt` verbatim — mind `mod`→`gmod`, `atan(y,x)`→`atan2`. Storage in/out, uniform PC block same fields. |
| C2 | `reduce_vec2.wgsl` + `reduce_vec4.wgsl` | ReduceRenderer.cpp:16-94 | compute 256 | `shared sh[256]` → `var<workgroup> sh: array<vec4f,256>`; `barrier()`→`workgroupBarrier()`; IDs→builtins; push consts → small uniform. Two source variants identical to native. |
| C3 | `eval_func.wgsl` | EvalRenderer.cpp:23-45 | compute | **Template with `{body}` substitution** — same wrapBody contract, WGSL skeleton. `FuncDef` op carries the body; interpreter caches pipeline per funcId. Body language: if `lang==glsl-expr`, run `glslExprToWgsl()` (§7.3) first. |
| C4 | `kde2d.wgsl` | KdeEvalRenderer.cpp:15-50 | compute 8×8 | Mechanical; 2D dispatch `dispatchWorkgroups(w/8, h/8)`. Output → storage buffer; also add `toTexture` variant writing rgba8 for direct DrawImage consumption (skip mailbox roundtrip when used purely for display — phase 2). |
| C5 | `hist_bins.wgsl` | HistPlot.cpp:59-… | compute 256 | `atomicAdd` on `array<atomic<u32>>` — direct port. |
| C6 | `kde1d.wgsl` | ViolinPlot.cpp:43-… | compute 256 | Mechanical. |
| C7 | `pcm_tess.wgsl` | PcolormeshPlot.cpp:26-95 | compute 256 | Cell→tris flat + gouraud center-vertex variant + LUT (`lut.c[256..258]` = under/over/bad). Mechanical; output = pos+col buffers consumed by `DrawTrisData` (GPU→GPU). |
| C8 | `env_cols.wgsl` | GpuLineRenderer.cpp (`envelopeColumns`) | compute | Min/max column envelope for huge-line decimation. Phase 2 — check native source when implementing. |
| — | *(skip)* `GpuPngEncoder`, `GpuYuvEncoders` | encode/*.cpp | compute | Not needed: `canvas.convertToBlob` (png/webp) + WebCodecs replace them. |

**Pipeline state table (TS side)** — mirror each native `init()`:

| Pipeline | Topology | Blend | Depth | MSAA | Buffers |
|---|---|---|---|---|---|
| px_tri / px_tri_vc / px_line(as tris) | triangle-list | src-alpha blend, dstAlpha: one | off | canvas MSAA | VB(s) via arena→scratch |
| instanced | triangle-list | same | off | MSAA | template VB + instance VB |
| text_quads | triangle-list | same | off | MSAA | quad VB + atlas tex |
| data_tri / data_line / points / pie | triangle-list | same | off | MSAA | storage/VB + uniform ring |
| heatmap | triangle-list (quad) | same | off | MSAA | 2 textures + sampler + uniform |
| surface3d | triangle-list indexed | same | **on (less)** | MSAA | VB+IB+uniform |
| grid3d | triangle-list (fs tri) | same | off | MSAA | uniform |

Blend state port — **carry the exact factor pairs from each `init()`** and
re-verify against the AGENTS.md alpha-overwrite note
(`srcAlphaBlendFactor`/`dstAlphaBlendFactor` must preserve framebuffer alpha:
`'zero'`/`'one'`, or whatever the current GLSL pipeline uses — diff each
pipeline against the regression suite's transparent-export test).

---

## 5. Emscripten build + JS-facing API

### 5.1 Build

New dir `web/` with `CMakeLists.txt` (or a `volcanoplot_web` target gated on
`EMSCRIPTEN`). Sources:

- all of `src/plot/**`
- `src/render/{Renderer.cpp,TickLayout.cpp,VectorRenderer.cpp,VectorWriters.cpp}`
- `src/text/{TextRenderer.cpp→refactored, MathText.cpp, glyb_msdf_stub.cpp}`
- `src/encode/` CPU encoders + SaveImage (PNG/WebP CPU paths — for
  `toBlob`-free export + tests), `src/encode/ExtraEncoders.cpp` (bmp/tiff)
- `dependencies/glyb` static lib target (already a cmake component)
- `src/web/*.cpp`
- **Exclude:** `src/core/**`, `src/backend/**`, all `src/render/primitives/*`
  Vulkan impls (Op* impls replace them), `src/encode/Gpu*`, CLI/examples/tests.

Flags:
```
-lembind -sEXPORT_ES6 -sMODULARIZE -sENVIRONMENT=web
-sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4294967296
-sUSE_FREETYPE=1 -sUSE_HARFBUZZ=1     # verify port names against installed emsdk
-fwasm-exceptions -std=c++23 -O2
--embed-file fonts/dejavu/DejaVuSans.ttf@/fonts/DejaVuSans.ttf  (or runtime fetch → setFontData)
```
glyb needs `glm` — vendored at `dependencies/glyb/third_party/glm`
(header-only, add include dir).

### 5.2 embind API (v1 — intentionally narrow; mirrors pybind names)

```cpp
// src/web/api.cpp
EMSCRIPTEN_BINDINGS(volcanoplot) {
  function("init",        &vp::init);                    // → u32 session
  function("figure",      &vp::figure);                  // (w,h,dpi) → u32 fig
  function("addAxes",     &vp::addAxes);                 // fig → u32 ax
  function("plot",        &vp::plot);                    // ax, F32Array x, y, string jsonKwargs → u32 h
  function("scatter",     &vp::scatter);
  function("bar", "hist", "imshow", "fillBetween", …);   // same pattern
  function("setXlim","setYlim","setTitle","setXLabel","setYLabel","setLog",…);
  function("legend",      &vp::legend);                  // ax, jsonKwargs
  function("colorbar",    &vp::colorbar);
  function("autoscale",   &vp::autoscale);
  function("renderFrame", &vp::renderFrame);             // fig → {ptr,len} (em::val views)
  function("frameDone",   &vp::frameDone);               // release frame buffers
  function("dispatchEvent",&vp::dispatchEvent);          // fig, jsonEvent
  function("toSvg",       &vp::toSvg);                   // → std::string
  function("toPdfBytes",  &vp::toPdf);                   // → em::val(Uint8Array)
  function("releaseFigure",&vp::releaseFigure);
  function("setFontData", &vp::setFontData);             // bytes → atlas mgr
  function("mailboxPoll", &vp::mailboxPoll);             // §6
}
```

kwargs as JSON strings (parse via the existing `Serialize.cpp` helpers where
applicable). Data via `emscripten::val` typed arrays → `memcpy` into series
storage. This is the only boundary where bulk data copies JS→WASM.

---

## 6. Async results: the mailbox pattern

WebGPU `mapAsync`/`onSubmittedWorkDone` are async; C++ autoscale/readback
paths are synchronous. **[FIXED] pattern:**

1. WASM exports `uint8_t* mailbox` — fixed array of `64 × vec4` slots + a
   `uint64_t readyMask`.
2. Ops that need results carry `mailbox` index k.
3. Interpreter, for each mailbox op: run the compute/readback, `mapAsync`,
   then `Module._vp_mailbox(k, a, b, c, d)` → C++ sets
   `readyMask |= 1<<k`, stores vec4, marks figure stale.
4. Next `renderIfStale` → ops re-emitted; the renderer-side impl reads the
   mailbox synchronously this time (result now available → e.g. viewport
   applied). One-frame latency, invisible in practice.
5. v1 may skip implementing 42–45/47 entirely — defaults already CPU-fallback.
   Implement mailbox with C2 (reduce) first since `contributeToAutoscaleGpu`
   is the cleanest call site.

---

## 7. GLSL→WGSL translation cookbook (for the junior)

### 7.1 Mechanical rules

| GLSL | WGSL |
|---|---|
| `layout(location=N) in/out T x` | `@location(N) x: T` in stage IO structs |
| `gl_Position = v` | `out.position = v` (`@builtin(position)`) |
| `gl_VertexIndex` / `gl_InstanceIndex` | `@builtin(vertex_index)` / `@builtin(instance_index)` |
| `gl_GlobalInvocationID` / `gl_LocalInvocationID` / `gl_WorkGroupID` | `@builtin(global_invocation_id)` / `…local_invocation_id` / `…workgroup_id` |
| `layout(local_size_x=N)` | `@workgroup_size(N)` above `@compute fn` |
| `layout(set=S,binding=B) uniform sampler2D t` | `@group(S) @binding(B) var t: texture_2d<f32>;` + separate `@group(S) @binding(B+1) var s: sampler;` |
| `texture(t, uv)` | `textureSample(t, s, uv)` |
| `texelFetch(t, ip, 0)` | `textureLoad(t, vec2u(ip), 0)` |
| `textureSize(t, 0)` | `vec2i(textureDimensions(t))` |
| `layout(push_constant) uniform PC {…}` | `@group(0) @binding(0) var<uniform> pc: PcStruct;` — interpreter binds a 256 B-aligned ring slot |
| `layout(set=…,binding=…) readonly buffer B { T d[]; }` | `@group(0) @binding(N) var<storage, read> B: array<T>;` |
| `… writeonly/buffer B` | `var<storage, read_write>` |
| `shared T sh[N]` | `var<workgroup> sh: array<T, N>` |
| `barrier()` | `workgroupBarrier()` |
| `float/vec2/vec3/vec4/mat4` | `f32/vec2f/vec3f/vec4f/mat4x4f` |
| `uint` | `u32` |
| `mix,clamp,fract,smoothstep,exp,log,pow,sign,abs,floor,round,min,max,length,normalize,dot,cross,sqrt` | identical names in WGSL |
| `fwidth,dpdx,dpdy` | `fwidth,dpdx,dpdy` (exist) |
| `discard` | `discard` |
| `isnan(x)` | `(x != x)` — no builtin |
| `atan(y, x)` | `atan2(y, x)` |
| `mat2(a,b,c,d)` | `mat2x2f(a,b,c,d)` — same column-major fill order |
| `ePointList`+`gl_PointSize`/`gl_PointCoord` | **gone** — instanced-quad rewrite (S6) |
| `eLineStrip`+`lineWidth` | 1px only — tessellation (S3/S5) or `line-list` for 1px |
| `eTriangleFan` | **gone** — convert fan→list indices CPU-side or emit `n-2` tris |
| vertex buffers `layout(location)` | WebGPU supports both vertex-buffer attrs AND storage-buffer vertex pulling — keep attrs where native had them, pulling where natural (S5) |

### 7.2 The `mod` trap

GLSL `mod(x,y) = x − y·floor(x/y)` (floor-mod). WGSL `%` on floats is
**trunc-mod** (`x − y·trunc(x/y)`) — different for negative x. `sdPoly` and
`arcFan` call `mod` on possibly-negative angles → artifacts if transliterated.
Add to `transform.wgsl`:

```wgsl
fn gmod(x: f32, y: f32) -> f32 { return x - y * floor(x / y); }
```

### 7.3 `glslExprToWgsl` (for `EvalFunc` bodies)

Users write `ax.plot("sin(x*freq)")`-style bodies; native compiles them as
GLSL. On web, transpile in TS: token-level rewrite —
`mod(`→`gmod(`, `atan(`→`atan2(` with arg-swap check (single-arg `atan`
stays `atan`), `isnan`→`!=`-form, numeric suffixes `u`/`U` strip, `float(`→`f32(`.
The math builtins (`sin cos tan exp log pow sqrt abs floor`) are identical.
Ship ~30 unit tests covering the body's documented builtin set; on
untranslatable input, fall back to CPU eval (the `evalCpu` path exists).

### 7.4 Alignment

WGSL uniform/storage layout ≈ std140/std430 rules but not identical — after
the AGENTS.md std140 bug, **verify every struct on both sides**: run each
shader's struct through WGSL validation (`device.pushErrorScope`) and assert
field offsets match the C++ payload layout (write a TS test that constructs
the UBO bytes and a WGSL test shader reading them — `tests/layout.test.ts`).

---

## 8. Streaming (the oscilloscope path)

`LineStreamPlot` (new plot type, C++): holds no samples — the **interpreter
owns the ring GPUBuffer**. C++ records
`DrawRingLines{buf, writeIndex, window, nChannels, transform, colorsBuf}`.
TS API:

```ts
const s = ax.lineStream({ channels: 5, windowSec: 5, rate: 1000 });
s.push(chunkF32);   // TS: wraps modulo writeBuffer chunks (blog code verbatim)
```
Vertex shader = blog's modulo-addressing math + scaleFwd/projFwd + width
extrusion (S5 ring variant). Sentinel `t = -1e30` init → `writeBuffer` once
at create. This is **the parity demo with the blog post** — milestone M4.

---

## 9. TypeScript package layout (`packages/volcanoplot/`)

```
packages/volcanoplot/
  package.json            # "volcanoplot", ESM, types, files:[dist, wasm]
  tsup.config.ts
  wasm/volcanoplot_web.wasm + .js   # emscripten output (build copies here)
  src/
    wasm.ts               # module init, HEAP views, ptr helpers
    api.ts                # TS facade: Figure/Axes/Plot classes (mirrors pybind names)
    opreader.ts           # binary cursor over the op region
    interpreter.ts        # main per-frame walk + deferred encode (§2 ordering)
    resources.ts          # handle→GPUBuffer/GPUTexture table, deferred destroy
    uniforms.ts           # 4MB uniform ring, 256B slots, per-frame reset
    pipelines.ts          # lazy pipeline registry keyed by (shaderId,msaa,fmt,blend)
    scratch.ts            # per-frame vertex scratch ring (mirrors SpineRenderer)
    text/atlas.ts         # nothing needed — wasm atlas (WriteTexture path)
    backend/canvas.ts     # configure(), MSAA/depth targets, resize, DPR
    backend/events.ts     # DOM→InputEvent JSON → dispatchEvent
    backend/export.ts     # toBlob/toDataURL via offscreen render + 2D canvas; SVG/PDF via wasm
    streaming/ringbuffer.ts # JS-owned ring buffer + push() (§8)
    shaders/*.wgsl        # §4 catalog
    shaders/index.ts      # `import src from './x.wgsl?raw'` (vite/tsup raw loading)
  tests/                  # vitest + playwright (§10)
  examples/oscilloscope.html|ts     # blog-parity demo
  examples/gallery.ts
```

`Interpreter.execute(region, frame)` skeleton:

```ts
const r = new OpReader(view, arenaBase);
for (const op of r) {
  switch (op.code) {
    case Op.CreateBuffer: …     // immediate
    case Op.WriteBuffer: queue.writeBuffer(buf(op.h), op.off, r.srcView(op.src)); break;
    case Op.DrawLines: draws.push({pipe:'data_line', ubo:op.ubo, scissor:op.clip, …}); uniforms.write(op.ubo); break;
    case Op.TessLines: computes.push(op); break;
    …
  }
}
queue.writeBuffer(uniformRing, 0, uniforms.flush());       // once
// compute pass (ops 40-47, record order) → render pass (ops 10-28)
queue.submit([encoder.finish()]);
resources.drainReleases();                                  // post-submit frees
```

Per-draw bind group: `createBindGroup({layout:auto-ish, entries:[{ring, offset:slot}]})`.
Use `hasDynamicOffset:true` on the uniform binding to avoid group-per-draw
explosion; one cached bind group per pipeline + `setBindGroup(0, g, [off])`.

---

## 10. Testing

1. **C++ golden op dumps** (no GPU): `PlotTestHarness` → render figure →
   `OpStream` (a `OpGpuServices` attached to a null backend also works
   natively — the op recorder has no Vulkan deps) → dump hex/op list to
   `tests/golden/ops/*.ops`. gtest diffs structural content (op sequence +
   scalar payloads; tolerate arena offset variance by canonicalizing).
2. **Interpreter unit tests** (vitest, no GPU needed for reader/uniform
   logic; GPU tests via `webgpu` polyfill in Deno or headless Chrome).
3. **Pixel parity** — Playwright + Chrome headless
   (`--enable-unsafe-swiftshader` for CI). Render fixture →
   `canvas.toDataURL`/`copyTextureToBuffer` → PNG → compare vs native
   headless PNG with tolerance (text/marker AA differs slightly under
   SwiftShader — assert structure + region colors like PlotTestHarness does,
   not exact pixels). Port the PlotTestHarness helpers (`countColor`,
   `boundingBox`, `centroid`, `averageRegion`) to ~150 lines of TS.
4. **Streaming soak test**: 60 s oscilloscope run, assert no handle leaks
   (`resources.size` bounded) and frame time < 16 ms p95.

---

## 11. Milestones (strict order; each ships working software)

| M | Deliverable | Ops/shaders needed | Acceptance |
|---|---|---|---|
| M0 | Phase-0 seam landed | — | all native tests green; bench ±2% |
| M1 | WASM builds; line plot in browser | CreateBuffer, WriteBuffer, DrawLines + S5+transform.wgsl | `example: ax.plot(sine)` renders, axes empty ok |
| M2 | Furniture: grid, spines, ticks, labels, legend | DrawTrisPx, DrawLineStripPx, DrawTextQuads + S1,S2,S3,S9 | mpl-default frame looks like native PNG |
| M3 | Scatter/fill/bar/hist | DrawPoints, DrawTrisData, DrawPie, DrawInstanced + S4,S6,S7,S8 | scatter markers (all shapes) render; bar/hist ok |
| M4 | Streaming oscilloscope | DrawRingLines + ringbuffer.ts + S5-ring | 5ch×1kHz at 60 fps, matches blog behavior |
| M5 | Heat+compute | DrawImage, TessLines, EvalFunc, PcmTess + S10,C1,C3,C7 | imshow+colorbar, pcolormesh, `ax.plot("expr")` GPU eval |
| M6 | 3D + export | DrawSurface, DrawGrid3D + S11,S12 + export.ts | surface renders w/ depth; toBlob png, toSvg, toPdf |
| M7 | Package + CI + docs | — | `npm pack` works; playwright CI green; docs/WEBGPU.md |

Phase-2 backlog (post-M7): C2 mailbox GPU autoscale, C4/C5/C6 mailbox paths,
C8 envelopes, MSDF text variant, glyb-atlas → browser-font atlas swap for
binary-size reduction, WebCodecs video export, wasm64, subgroups.

---

## 12. Risk register (what a junior should watch)

| Risk | Watch for |
|---|---|
| Push-const→uniform layout mismatch | §7.4 layout test per shader BEFORE visual debugging |
| `mod`/`atan` transliteration bugs | §7.2; first symptom: star/polygon markers distorted, arc joins wrong direction |
| WASM heap detach on growth | re-acquire `HEAPU8.buffer` views every frame; never cache `.buffer` |
| Atlas/font init ordering | `setFontData` before first text op; WriteTexture-atlas ops precede DrawTextQuads in same frame |
| Scissor conventions | identical top-left in both APIs — do NOT flip Y in scissor; Y-flip lives only in NDC math (copied verbatim) |
| Release-before-submit | deferred releases (mirrors retired_ bug from AGENTS.md) |
| WriteBuffer of arena slices mid-pass | record all writeBuffers BEFORE creating pass encoders, or accept spec-allowed interleave — prefer §9 two-phase walk |
| 256 B uniform alignment | slot size = ceil(blob,256); assert at bind time |
| Emscripten exceptions | `-fwasm-exceptions` required (code throws) |
| glyb/harfbuzz ports missing | fallback: `-sUSE_FREETYPE=1` exists; harfbuzz may need `embuilder`/vendored build — spike this in M2 first |

## 13. Open items (only these are undecided)

1. npm package name + whether wasm ships inlined-base64 or separate asset
   (recommend separate + `init({wasmUrl})`, inline option via `?inline`).
2. Whether `drawTicks`-level helpers stay C++ (decompose to segments op) —
   recommended yes: fewer ops.
3. Font set shipped in wasm (DejaVu regular + bold + oblique ≈ 2 MB; subset
   to latin-1 ≈ 300 KB — recommend subset + runtime `setFontData`).

---

## 14. Implementation status (as built)

**Verified in real Chrome + SwiftShader: 43/43 browser tests (incl.
DrawTrisData viewport/orientation regression specs), 8/8 vitest,
1616/1616 native tests. 33-case matplotlib comparison gallery
regenerable via `scripts/generate_webgallery.py`.**

| Component | Status |
|---|---|
| VPOP stream + all Op renderers | done |
| embind API: 41 plot types bound + setData + subplot + events + axes styling (xlim/ylim/scales/title/labels/grid), reference lines/spans, legend/colorbar/text | done |
| WGSL pipelines (lines/points/tris/instanced/image/text/surface/grid3D) | done |
| Compute: TessLines, ReduceMinMax (autoscale), FuncDef/EvalFunc, KdeEval2D, PcmTess, ViolinKde (kde1d) | done |
| Text atlas (glyb→WASM, fonts via --preload-file) | done |
| Mailbox: 4-float (autoscale) + bulk bytes (KDE grids) via _vp_mailboxDest/Done | done |
| Depth pass (3D surface) + depth-compatible 2D pipeline variants | done |
| Canvas2D fallback (auto when no WebGPU) | done |
| SVG vector export (`vp.toSvg()`) | done |
| Interaction (Navigation pan/zoom via Figure::dispatch) | done |
| Multi-axes (`vp.subplot()`) | done |
| Device-lost handling, adapter retention | done |
| npm packaging + README | done |
| Comparison gallery (web/gallery: 33 mpl-vs-WebGPU side-by-side PNGs via `scripts/generate_webgallery.py`; deterministic LCG data shared between JS demo and mpl script) | done |
| HistBins GPU | skipped deliberately — GPU binning is slower than 8-thread CPU even natively (measured); opt-in upstream |
| Text as vector outlines in SVG export | open — glyphs are atlas-rasterized upstream |
| Canvas2D fallback for 3D ops | open — surface/grid3D degrade to nothing |
| Remaining unbound plot types (Bar3D, Plot3D, Scatter3D, Voxels, barbs, eventplot, figimage, table, NavCube, ...) | open — thin wrappers, same pattern |

Notable divergences from the plan text: single session-long OpStream reset at
render start (repaints append to the same frame — `finish()` must not consume
records); buffer-kind bitmask extended with bit4 COPY_SRC for readback
sources; mailbox is two channels (4-float + arbitrary bytes), not one.
