// web/src/shaders/DrawTrisData.wgsl — data-space triangle soup
// (FillRenderer / pie-affine variants). Vertex-pulling.
//
//   0 uniform Xform
//   1 storage pos : array<vec2f>
//   2 storage col : array<vec4f>   (optional — HAS_COL)
//
// MODE PIE: positions are pie data units; ubo.rect = {cx, cy, sc, sc}
// pixel center + half-min scale; px = rect.xy + pos * rect.z * 0.8.

// #include transform.wgsl

@group(0) @binding(0) var<uniform> U : Xform;
@group(0) @binding(1) var<storage, read> pos : array<vec2f>;
#ifdef HAS_COL
@group(0) @binding(2) var<storage, read> cols : array<vec4f>;
#endif

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    var o : VSOut;
#ifdef MODE_PIE
    // pie-data units → framebuffer px → viewport NDC (viewport = clip)
    let px = U.rect.xy + pos[vi] * U.rect.z * 0.8;
    let ndc = vec2f(px.x / U.extra2.x * 2.0 - 1.0,
                    1.0 - px.y / U.extra2.y * 2.0);   // extra2=canvasWH
    o.p = vec4f(ndc, 0.0, 1.0);
#else
    let p = projFwd(vec2f(scaleFwd(pos[vi].x, U.scaleX),
                          scaleFwd(pos[vi].y, U.scaleY)),
                    U.proj.xyz);
    let ndc = (p - U.viewMinSpan.xy) / U.viewMinSpan.zw * 2.0 - 1.0;
    o.p = vec4f(ndc.x, -ndc.y, 0.0, 1.0);
#endif
#ifdef HAS_COL
    o.color = cols[vi];
#else
    o.color = U.color;
#endif
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
