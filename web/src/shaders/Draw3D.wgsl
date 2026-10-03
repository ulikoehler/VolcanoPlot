// Draw3D.wgsl — GPU-projected 3D primitives (DrawSegs3D, DrawTris3D,
// DrawPoints3D). Raw vec3 world vertices + the camera VP live on the
// device; this shader reproduces the CPU projectPoint3D() math exactly
// (NDC, y flipped) so the rendered pixels are identical to the CPU
// fallback. Rasterization is painter's-order with constant z — same
// as the 2D paths these ops replace.
//
//   0 uniform ProjU
//   1 storage pos  : array<u32>   (C++ Point3D = 12 B → u32 triples)
//                 MODE_BOXES: array<BoxInst> {vec3f o; vec3f s; vec4f c}
//   2 storage col  : array<vec4f> (optional — HAS_COL)
//   3 storage size : array<f32>   (optional — HAS_SIZE, points only)
//
// MODE_SEGS   → line-list
// MODE_TRIS   → triangle-list
// MODE_POINTS → triangle-list, 6 verts per marker quad (markerDist
//               comes from markers.wgsl)
// MODE_BOXES  → instanced axis-aligned boxes (DrawBoxes3D): one instance
//               per box, the VS expands a 36-vert unit cube. Unlike the
//               painter's-order modes this keeps the real clip-space z
//               and runs depth-tested — correct occlusion with zero CPU
//               sorting.

struct ProjU {
    vp     : mat4x4f,   // column-major (row-major M transposed → m*p = M p)
    rect   : vec4f,     // axes pixel rect (x, y, w, h)
    color  : vec4f,     // flat color when no per-vertex buffer
    misc   : vec4f,     // x = line width (unused — line-list is 1 px)
    marker : vec4f,     // MarkerParams {code, fill, numsides, rot}
};

#ifdef MODE_BOXES
struct BoxInst { o : vec3f, s : vec3f, c : vec4f };
#endif
@group(0) @binding(0) var<uniform> U : ProjU;
#ifdef MODE_BOXES
@group(0) @binding(1) var<storage, read> insts : array<BoxInst>;
#else
@group(0) @binding(1) var<storage, read> pos : array<u32>;
#endif
#ifdef HAS_COL
@group(0) @binding(2) var<storage, read> cols : array<vec4f>;
#endif
#ifdef HAS_SIZE
@group(0) @binding(3) var<storage, read> sizes : array<f32>;
#endif
// Painter's-order index buffer produced by the DepthSort compute op:
// vertex_index walks the sorted triangle order instead of the buffer.
#ifdef IDX
@group(0) @binding(4) var<storage, read> indices : array<u32>;
#endif

fn vidx(vi : u32) -> u32 {
#ifdef IDX
    return indices[vi];
#else
    return vi;
#endif
}

fn glslMod(x : f32, y : f32) -> f32 { return x - y * floor(x / y); }

#ifdef MODE_BOXES
#else
fn pos3(vi : u32) -> vec3f {
    return vec3f(bitcast<f32>(pos[vi * 3u]),
                 bitcast<f32>(pos[vi * 3u + 1u]),
                 bitcast<f32>(pos[vi * 3u + 2u]));
}
#endif

/// projectPoint3D: clip = vp * p → NDC (x/w, -y/w). Degenerate w → 0.
/// Constant z = 0.5 keeps the painter's-order semantics of the 2D ops.
fn proj(p : vec3f) -> vec4f {
    let c = U.vp * vec4f(p, 1.0);
    if (abs(c.w) < 1e-30) { return vec4f(0.0, 0.0, 0.5, 1.0); }
    return vec4f(c.x / c.w, -c.y / c.w, 0.5, 1.0);
}

/// NDC → framebuffer px (y-down) inside the axes rect.
fn ndcToPx(n : vec2f) -> vec2f {
    return vec2f(U.rect.x + (n.x * 0.5 + 0.5) * U.rect.z,
                 U.rect.y + (0.5 - n.y * 0.5) * U.rect.w);
}
/// px inside the axes rect → NDC for the viewport set to that rect.
fn pxToNdc(px : vec2f) -> vec2f {
    return vec2f((px.x - U.rect.x) / U.rect.z * 2.0 - 1.0,
                 1.0 - (px.y - U.rect.y) / U.rect.w * 2.0);
}

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) color : vec4f,
#ifdef MODE_POINTS
    @location(1) local : vec2f,
    @location(2) size : f32,
#endif
};

#ifdef MODE_BOXES
// Same face order as Bar3D/VoxelsPlot: -X +X -Y +Y -Z +Z; corner index
// bits are +x/+y/+z offsets from the box origin.
const FACE_CORNERS : array<array<u32, 4>, 6> = array<array<u32, 4>, 6>(
    array<u32, 4>(0u, 2u, 3u, 1u), array<u32, 4>(4u, 5u, 7u, 6u),
    array<u32, 4>(0u, 1u, 5u, 4u), array<u32, 4>(2u, 6u, 7u, 3u),
    array<u32, 4>(0u, 4u, 6u, 2u), array<u32, 4>(1u, 3u, 7u, 5u));
const TRI_CORNER : array<u32, 6> =
    array<u32, 6>(0u, 1u, 2u, 0u, 2u, 3u);
// Matches the CPU faceShade table in Bar3D.cpp / VoxelsPlot.cpp.
const FACE_SHADE : array<f32, 6> =
    array<f32, 6>(0.7, 0.85, 0.6, 0.8, 0.5, 1.0);

@vertex fn vs(@builtin(vertex_index) vi : u32,
              @builtin(instance_index) inst : u32) -> VSOut {
    let b = insts[inst];
    let face = vi / 6u;
    let ci = FACE_CORNERS[face][TRI_CORNER[vi % 6u]];
    let corner = vec3f(f32(ci & 1u), f32((ci >> 1u) & 1u),
                       f32((ci >> 2u) & 1u));
    let c = U.vp * vec4f(b.o + corner * b.s, 1.0);
    var o : VSOut;
    if (abs(c.w) < 1e-30) { o.p = vec4f(0.0, 0.0, 0.5, 1.0); }
    else { o.p = vec4f(c.x, -c.y, c.z, c.w); }   // real z → depth test
    let shade = FACE_SHADE[face];
    o.color = vec4f(b.c.rgb * shade, b.c.a);
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
#else
#ifdef MODE_POINTS
const CORNERS : array<vec2f, 6> = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));

@vertex fn vs(@builtin(vertex_index) vi : u32,
              @builtin(instance_index) inst : u32) -> VSOut {
    let c = CORNERS[vi];
    var sz = 6.0;
#ifdef HAS_SIZE
    sz = max(sizes[inst], 0.5);
#endif
    let ndc = proj(pos3(inst)).xy;
    let px = ndcToPx(ndc) + c * (sz * 0.5 + 1.0);
    var o : VSOut;
    o.p = vec4f(pxToNdc(px), 0.0, 1.0);
    o.local = c * (sz * 0.5 + 1.0);
    o.size = sz;
#ifdef HAS_COL
    o.color = cols[inst];
#else
    o.color = U.color;
#endif
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    let code = i32(U.marker.x + 0.5);
    let nside = i32(U.marker.z + 0.5);
    let rot = U.marker.w;
    let c = v.local / (v.size * 0.5);
    let d = markerDist(c, code, nside, rot);
    let w = fwidth(d);
    let alpha = 1.0 - smoothstep(-w, w, d);
    if (alpha <= 0.001) { discard; }
    return vec4f(v.color.rgb, v.color.a * alpha);
}
#else
@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    var o : VSOut;
    let v = vidx(vi);
    o.p = proj(pos3(v));
#ifdef HAS_COL
    o.color = cols[v];
#else
    o.color = U.color;
#endif
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
#endif
#endif
