// web/src/shaders/DrawLines.wgsl — port of LineRenderer.cpp's GLSL.
// Vertex-pulls from a storage buffer (kind=VertexStorage); equivalent to
// the native vertex-attribute path. Polyline expansion for width > 1 is
// the TessLines compute op's job — this pipeline draws the 1px strip.

// #include transform.wgsl — inlined by the TS pipeline builder.

@group(0) @binding(0) var<uniform> U : Xform;
@group(0) @binding(1) var<storage, read> pts : array<vec2f>;

struct VSOut {
    @builtin(position) pos : vec4f,
    @location(0) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32,
              @builtin(instance_index) base : u32) -> VSOut {
    let d = pts[base + vi];
    let p = projFwd(vec2f(scaleFwd(d.x, U.scaleX),
                          scaleFwd(d.y, U.scaleY)),
                    U.proj.xyz);
    // data → NDC; viewport rect applied via pass.setViewport (viewRect)
    let ndc = (p - U.viewMinSpan.xy) / U.viewMinSpan.zw * 2.0 - 1.0;
    var o : VSOut;
    o.pos = vec4f(ndc.x, -ndc.y, 0.0, 1.0);  // Y-up → NDC
    o.color = U.color;
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
