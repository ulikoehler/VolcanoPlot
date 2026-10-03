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
                      uint32_t vertBuf, idxBuf, indexCount; };
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
#pragma pack(pop)

} // namespace volcano::web
