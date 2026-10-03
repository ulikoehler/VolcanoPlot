// src/web/OpPayloads.hpp — packed payload structs for each op (§2)
//
// Layout must match web/src/opcodes.ts byte-for-byte. All structs are
// trivially copyable, little-endian (WASM/JS are LE on all targets).
#pragma once

#include "OpStream.hpp"
#include <volcano/plot/Transform.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/primitives/InstancedPathRenderer.hpp>

namespace volcano::web {

/// The 6-vec4 + extras push block, verbatim from the Vulkan impls.
/// Keep field order identical to each renderer's PC struct.
struct TransformUBO {
    float viewMinX, viewMinY, viewSpanX, viewSpanY;  // u_viewMinSpan
    float rectX, rectY, rectW, rectH;                // u_rect
    float r, g, b, a;                                 // u_color
    float sxCode, sxP1, sxP2, sxPad;                  // u_scaleX
    float syCode, syP1, syP2, syPad;                  // u_scaleY
    float prCode, thetaOff, thetaDir, prPad;          // u_proj
    float extra[8];                                   // u_width/u_marker…
};

[[nodiscard]] inline TransformUBO makeTransformUBO(
    const plot::Transform2D& t, plot::Rect2D rect, plot::Color c) {
    TransformUBO u{};
    u.viewMinX = t.view.x.min;
    u.viewMinY = t.view.y.min;
    u.viewSpanX = t.view.x.span();
    u.viewSpanY = t.view.y.span();
    u.rectX = float(rect.x); u.rectY = float(rect.y);
    u.rectW = float(rect.width); u.rectH = float(rect.height);
    u.r = c.r; u.g = c.g; u.b = c.b; u.a = c.a;
    u.sxCode = float(int(t.codeX()));
    u.sxP1 = t.scaleX.param1; u.sxP2 = t.scaleX.param2;
    u.sxPad = t.scaleX.param3;
    u.syCode = float(int(t.codeY()));
    u.syP1 = t.scaleY.param1; u.syP2 = t.scaleY.param2;
    u.syPad = t.scaleY.param3;
    u.prCode = float(int(t.projection.kind));
    u.thetaOff = t.projection.thetaOffset;
    u.thetaDir = t.projection.thetaDir;
    return u;
}

[[nodiscard]] inline Rect2Df toF(plot::Rect2D r) {
    return {float(r.x), float(r.y), float(r.width), float(r.height)};
}

#pragma pack(push, 1)
// ── resource ops ──
struct PCreateBuffer { uint32_t handle; uint64_t size; uint8_t kind; };
struct PWriteBuffer  { uint32_t handle; uint64_t dstOff; BufSrc data; };
struct PReleaseBuffer{ uint32_t handle; };
struct PCreateTexture{ uint32_t handle, w, h; uint8_t fmt; };
struct PWriteTexture { uint32_t handle, x, y, w, h; BufSrc data; };
struct PReleaseTexture{ uint32_t handle; };

// ── pixel-space draws ──
struct PDrawTrisPx     { Rect2Df clip; BufSrc verts; float r,g,b,a; };
struct PDrawTrisPxVC   { Rect2Df clip; BufSrc verts; BufSrc colors; };
struct PDrawLinePx     { Rect2Df clip; BufSrc pts; float r,g,b,a,wPx; };
struct PDrawTextQuads  { Rect2Df clip; uint32_t atlasTex; BufSrc quads; };
struct PDrawInstanced  { Rect2Df clip; BufSrc tpl; BufSrc inst;
                         TransformUBO ubo; };

// ── data-space draws ──
struct PDrawLines   { Rect2Df clip; Rect2Df viewRect; TransformUBO ubo;
                      uint32_t buf, count; float r,g,b,a, width; };
struct PDrawPoints  { Rect2Df clip; Rect2Df viewRect; TransformUBO ubo;
                      uint32_t posBuf, colBuf, sizeBuf, count;
                      uint32_t flags;  // bit0 hasCol, bit1 hasSize
                      float marker[4]; };
struct PDrawTrisData{ Rect2Df clip; TransformUBO ubo;
                      uint32_t posBuf, colBuf, vertCount;
                      float r,g,b,a; };
struct PDrawTrisGpu { Rect2Df clip; float resW, resH;
                      uint32_t buf; uint64_t byteOff; uint32_t count;
                      /// Non-zero → the vertex count comes from this
                      /// buffer's first u32 (indirect draw), so a
                      /// compute pass can size the mesh on the device.
                      uint32_t countBuf; };
struct PDrawImage   { Rect2Df viewRect; TransformUBO ubo;
                      uint32_t gridTex, cmapTex; float params[8]; };

// ── 3D draws ──
/// vp is column-major mat4 (transposed from Camera3D::viewProjection's
/// row-major layout — WGSL mat4x4f is column-major).
struct PDrawSurface { Rect2Df clip; float vp[16]; float gridRange[4];
                      float light[4]; float valueMin, valueMax;
                      float pad[2];
                      uint32_t vertBuf, idxBuf, indexCount;
                      /// Vertex-pull grid mode (`surfacemesh` offload):
                      /// idxBuf==0 → vertBuf is a flat f32 z array of
                      /// gridW*gridH; the VS derives positions and cell
                      /// topology from vertex_index. 0 = CPU-tessellated
                      /// indexed mesh.
                      uint32_t gridW, gridH; };
/// The full 44-float push block from Grid3DRendererVk::draw.
struct PDrawGrid3D { Rect2Df clip; float pc[44]; };

/// Shared payload for the GPU-projection 3D ops (DrawSegs3D /
/// DrawTris3D / DrawPoints3D): raw vec3f vertices in `posBuf` (C++
/// Point3D = 12 B records — the shader pulls u32 triples), projected
/// by `vp` in the vertex shader. `vp` is column-major (transposed from
/// the row-major Camera3D matrix, matching PDrawSurface). Rasterization
/// is painter's-order with constant depth — identical pixels to the
/// CPU project-then-draw path.
///   clip      @0   scissor rect
///   view      @16  axes pixel rect (viewport)
///   vp        @32  column-major view-projection
///   posBuf    @96  vec3f storage buffer
///   colBuf    @100 per-vertex vec4f colors (0 = flat `rgba`)
///   auxBuf    @104 per-point f32 sizes (points only, 0 = none)
///   count     @108 vertices (segs/tris) / points (points)
///   rgba      @112 flat color
///   width     @128
///   flags     @132 points: bit0 hasCol, bit1 hasSize
///   marker    @136 MarkerParams {code, fill, numsides, angle}
struct PDraw3D { Rect2Df clip; Rect2Df view; float vp[16];
                 uint32_t posBuf, colBuf, auxBuf, count;
                 float r, g, b, a, width;
                 uint32_t flags; float marker[4]; };

// ── compute ops ──
struct PTessLines  { uint32_t inBuf, inBase, outBuf, outBase;
                     uint32_t n, nSeg; float hwidth;
                     uint8_t join, cap; float miterLimit;
                     float r,g,b,a;
                     /// Dashes: per-point cumulative arc length (0 when
                     /// the stroke is solid), the pattern, and how many
                     /// dash slots a segment reserved. `dashMul` 1 keeps
                     /// the solid layout (6 verts/segment, joins after).
                     uint32_t lenBuf, dashBuf, dashCount, dashMul;
                     float dashOffset, pad0, pad1, pad2; };
struct PEvalFunc   { uint32_t outBuf; double xMin, xMax;
                     uint32_t count; uint16_t funcId; uint8_t pad[6]; };
struct PFuncDef    { uint16_t funcId; uint8_t lang; BufSrc body; };
struct PReduceMinMax{ uint32_t inBuf, count, mailbox; };
struct PHistBins   { uint32_t srcBuf, n, binsBuf;
                     float e0, invW; uint32_t nBins, mailbox; };
/// 2D KDE: samples (f32 x,y pairs) → grid densities. Result is read
/// back via bulk mailbox (slot → gridW*gridH f32).
struct PKdeEval2D  { uint32_t inBuf, n, outBuf, gridW, gridH;
                     float xMin, xStep, yMin, yStep;
                     float inv2bwX2, inv2bwY2, norm;
                     uint32_t mailbox; };
/// Pcolormesh tessellation: x/y edge arrays + normalized t + 259-entry
/// vec4 LUT → pos (vec2)/col (vec4) vertex buffers consumed by a later
/// DrawTrisData in the same stream. flags: bit0 cmap.bad, bit1 skipNaN.
struct PPcmTess    { uint32_t xBuf, yBuf, tBuf, lutBuf, posBuf, colBuf;
                     uint32_t nCols, nRows, gouraud, flags; };
/// 1-D Gaussian KDE: samples → `ne` densities at lo + i*step.
/// Result read back via bulk mailbox (slot → ne f32).
struct PViolinKde  { uint32_t inBuf, n, outBuf, ne;
                     float lo, step, bw; uint32_t mailbox; };
/// 2-D histogram binning: x/y sample pairs → nBinsX*nBinsY counts
/// (row-major, y-major). Uniform edges: bin = (v - e0) * invW.
/// Result read back via bulk mailbox (slot → nBinsX*nBinsY u32).
struct PHistBins2D { uint32_t xyBuf, n, binsBuf;
                     float x0, invWX, y0, invWY;
                     uint32_t nBinsX, nBinsY, mailbox; };
/// Grid contour tessellation: marching squares + stroke expansion into
/// a triangle soup of {vec2 pos_px, vec4 rgba} records, with an atomic
/// vertex counter. `counterBuf` doubles as the indirect draw argument
/// buffer ({vertexCount, 1, 0, 0} — the C++ side seeds instanceCount).
/// Data → pixel affine: px = bx + v * ax, py = by + v * ay.
struct PContourTess { uint32_t gridBuf, levelsBuf, colBuf, outBuf;
                      uint32_t counterBuf, gridW, gridH, nLevels;
                      float bx, ax, by, ay;
                      float hwidth, pad0;
                      uint32_t maxVerts, dashBuf;
                      /// Max dashes a single segment may emit — sizes
                      /// the soup when any level is dashed.
                      uint32_t dashMul, pad1; };
/// Batched real-input FFT: `numSegs` windows of `n` samples (hop
/// `step`) from `sigBuf`, multiplied by the `n`-sample `winBuf`, then
/// transformed. Output is `numSegs * n` complex values interleaved
/// (re, im) — read back via bulk mailbox (slot → numSegs*n*2 f32).
/// The per-plot spectral math (power, cross spectra, unwrapping) stays
/// on the CPU, so every spectrum family member shares one primitive.
struct PFftSegments { uint32_t sigBuf, winBuf, outBuf;
                      uint32_t n, step, numSegs, sigLen, mailbox; };
/// mpl hexbin (pointy-top) lattice counts: two interleaved lattices
/// A (nx+1)x(ny+1) at (i*sx, j*sy) and B nx*ny offset by (+.5sx,+.5sy).
/// Result read back via bulk mailbox
/// (slot → [(nx+1)*(ny+1) + nx*ny] u32, A then B).
struct PHexBins    { uint32_t xyBuf, n, outBuf;
                     float xMin, yMin, sx, sy;
                     uint32_t nx, ny, mailbox; };
/// Per-pixel-column min/max envelope over a vec2f point buffer (port of
/// GpuLineRendererVk::envelopeColumns' GLSL). One invocation per
/// segment (i-1, i) accumulates ordered-int min/max keys per column —
/// works on unsorted x. outBuf = u32[2*nCols]: [0..n) mn keys seeded
/// 0xFF800000, [n..2n) mx keys seeded 0x007FFFFF. Read back via bulk
/// mailbox (slot → nCols*2 u32); the C++ side unords to floats.
struct PEnvelopeCols { uint32_t xyBuf, n, outBuf;
                       float ax, kx;
                       int32_t cx0, cx1;
                       uint32_t nCols, mailbox; };
/// Scattered-data contour tessellation (marching triangles). One
/// invocation per (triangle, level|band). xyzBuf packs (x, y, z) f32
/// triples per point; trisBuf packs 3 u32 indices per triangle.
/// mode 0 = isolines (stroke each crossing segment like ContourTess,
/// colors/dashes per level); mode 1 = filled bands (Sutherland–Hodgman
/// clip of each triangle to [level_i, level_{i+1}], band color).
/// Output = {vec2 pos_px, vec4 rgba} soup + indirect args — same draw
/// path as ContourTess.
struct PTriContourTess { uint32_t xyzBuf, trisBuf, levelsBuf, colBuf,
                         outBuf, counterBuf, dashBuf;
                         uint32_t nTris, nLevels;
                         float bx, ax, by, ay;
                         float hwidth; uint32_t mode, maxVerts, dashMul; };
/// Quiver arrowhead expansion (marching-heads): segsBuf packs
/// {x0, y0, x1, y1} pixel-space shaft endpoints per arrow. mode 0 =
/// simple triangle head, mode 1 = mpl notched head. Output = indirect
/// triangle soup (same draw contract as ContourTess).
struct PQuiverTess { uint32_t segBuf, outBuf, counterBuf;
                     uint32_t n, mode, maxVerts;
                     float hw2, hl, hal, pad;
                     float r, g, b, a; };
/// fill_between band tessellation: x/y1/y2 are the curves, maskBuf one
/// u32 (0/1) per point — the CPU-side `where` ∧ finite mask. flags bit0
/// = interpolate (trim run boundaries to the f1 == f2 crossing).
/// Output = indirect triangle soup.
struct PFillBetweenTess { uint32_t xBuf, y1Buf, y2Buf, maskBuf;
                          uint32_t outBuf, counterBuf;
                          uint32_t n, flags, maxVerts;
                          float bx, ax, by, ay;
                          float r, g, b, a; };
/// GPU RK4 streamline tracing: one invocation per candidate seed.
/// outPts holds 2 × maxPoints (x, y) pairs per seed — the backward run
/// first, then the forward one; outCnt the two per-seed lengths.
struct PStreamlines { uint32_t uBuf, vBuf, seedBuf, outPts, outCnt;
                      uint32_t w, h, maxPoints, flags, nSeeds;
                      float xMin, xSpan, yMin, ySpan, stepSize;
                      uint32_t slot; };
/// Painter's-order 3D triangle sort: `posBuf` holds the packed vec3
/// vertices, `idxBuf` the 3-per-triangle index list (seeded to the
/// identity by the key pass), `keyBuf` one f32 per triangle. `vp` is the
/// row-major view-projection the keys are derived from.
struct PDepthSort { uint32_t posBuf, idxBuf, keyBuf;
                    uint32_t nTris, nPad;
                    float vp[16]; };
#pragma pack(pop)

} // namespace volcano::web
