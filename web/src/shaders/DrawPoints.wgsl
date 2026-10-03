// web/src/shaders/DrawPoints.wgsl — PointRenderer port.
// WebGPU has no point sprites: each point expands to a 6-vertex
// billboard quad in the vertex shader; the fragment shader evaluates the
// marker SDF (same codes as the GLSL impl — subset for v1).
//
//   0 uniform Xform (transform.wgsl)
//   1 storage pos  : array<vec2f>
//   2 storage col  : array<vec4f>   (optional — HAS_COL)
//   3 storage size : array<f32>     (optional — HAS_SIZE)

// #include transform.wgsl

@group(0) @binding(0) var<uniform> U : Xform;
@group(0) @binding(1) var<storage, read> pts : array<vec2f>;
#ifdef HAS_COL
@group(0) @binding(2) var<storage, read> cols : array<vec4f>;
#endif
#ifdef HAS_SIZE
@group(0) @binding(3) var<storage, read> sizes : array<f32>;
#endif

fn dataToPx(d : vec2f) -> vec2f {
    let p = projFwd(vec2f(scaleFwd(d.x, U.scaleX),
                          scaleFwd(d.y, U.scaleY)),
                    U.proj.xyz);
    // data → viewport-relative px: [0,1] over view → rect
    let t = (p - U.viewMinSpan.xy) / U.viewMinSpan.zw;
    return vec2f(U.rect.x + t.x * U.rect.z,
                 U.rect.y + U.rect.w - t.y * U.rect.w);  // data Y-up
}

const CORNERS : array<vec2f, 6> = array<vec2f, 6>(
    vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
    vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));

struct VSOut {
    @builtin(position) pos : vec4f,
    @location(0) color : vec4f,
    @location(1) local : vec2f,   // quad-local coords, ±size/2
    @location(2) size : f32,
};

@vertex fn vs(@builtin(vertex_index) vi : u32,
              @builtin(instance_index) inst : u32) -> VSOut {
    let c = CORNERS[vi];
    var sz = 6.0;
#ifdef HAS_SIZE
    sz = max(sizes[inst], 0.5);
#endif
    let center = dataToPx(pts[inst]);
    let px = center + c * (sz * 0.5 + 1.0);
    var o : VSOut;
    // px → NDC via canvas dims packed in viewMinSpan? No: px shaders
    // use viewport — the pass viewport is set to `viewRect`, so emit
    // NDC in viewport space: px/rect → [0,1] → NDC.
    let ndc = vec2f((px.x - U.rect.x) / U.rect.z * 2.0 - 1.0,
                    1.0 - (px.y - U.rect.y) / U.rect.w * 2.0);
    o.pos = vec4f(ndc, 0.0, 1.0);
    o.local = c * (sz * 0.5 + 1.0);
    o.size = sz;
#ifdef HAS_COL
    o.color = cols[inst];
#else
    o.color = U.color;
#endif
    return o;
}

// markerDist lives in markers.wgsl (prepended via the `markers` pipe
// flag — shared with Draw3D.wgsl's points mode).

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    let code = i32(U.extra.x + 0.5);
    let nside = i32(U.extra.z + 0.5);
    let rot = U.extra.w;
    // normalize local → marker units (marker radius ≈ size/2)
    let c = v.local / (v.size * 0.5);
    let d = markerDist(c, code, nside, rot);
    let w = fwidth(d);
    let alpha = 1.0 - smoothstep(-w, w, d);
    if (alpha <= 0.001) { discard; }
    return vec4f(v.color.rgb, v.color.a * alpha);
}
