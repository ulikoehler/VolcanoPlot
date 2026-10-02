// web/src/shaders/DrawImage.wgsl — HeatmapRenderer port.
// Full-rect textured quad; grid texture (r32float or rgba8) sampled at
// cell centers, mapped through a 256×1 colormap LUT.
//
//   0 uniform Xform (rect = image rect px; params via extra/extra2)
//   1 texture gridTex  : texture_2d<f32>
//   2 texture cmapTex  : texture_2d<f32>
//   3 sampler samp     : sampler (nearest)
//
// Params layout (PDrawImage.params[8] → Xform.extra/extra2):
//   extra = [rgbaMode, originLower, nanTransparent, valueMin]
//   extra2 = [valueMax, xMin, yMin, gridW]

// #include transform.wgsl

@group(0) @binding(0) var<uniform> U : Xform;
@group(0) @binding(1) var gridTex : texture_2d<f32>;
@group(0) @binding(2) var cmapTex : texture_2d<f32>;
@group(0) @binding(3) var samp : sampler;

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) uv : vec2f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    // fullscreen-quad 6 verts over the image rect (viewport=viewRect)
    let CORNERS = array<vec2f, 6>(
        vec2f(0.,0.), vec2f(1.,0.), vec2f(1.,1.),
        vec2f(0.,0.), vec2f(1.,1.), vec2f(0.,1.));
    let t = CORNERS[vi];
    var o : VSOut;
    o.p = vec4f(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);
    // originLower: row 0 at bottom → v = 1-t.y
    var v = t.y;
    if (U.extra.y > 0.5) { v = 1.0 - t.y; }   // originLower
    o.uv = vec2f(t.x, v);
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    let px = textureSample(gridTex, samp, v.uv);
    if (U.extra.x > 0.5) {                      // rgbaMode: raw colors
        return px;
    }
    let lo = U.extra.w;                         // valueMin
    let hi = U.extra2.x;                        // valueMax
    var t = (px.x - lo) / max(hi - lo, 1e-30);
    if (U.extra.z > 0.5 && (px.x != px.x)) {    // NaN → transparent
        discard;
    }
    t = clamp(t, 0.0, 1.0);
    return textureSample(cmapTex, samp, vec2f(t, 0.5));
}
