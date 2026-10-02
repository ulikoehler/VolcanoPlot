// web/src/shaders/px.wgsl — pixel-space primitives (SpineRenderer port).
// Positions are framebuffer pixels, Y-down; WebGPU NDC is Y-up, so the
// shader negates Y (native relied on Vulkan's Y-down NDC).
//
// Bindings:
//   0 uniform PxUBO { canvasWH : vec2f, color : vec4f }
//   1 storage pos : array<vec2f>
//   2 storage col : array<vec4f>   (VC variant only)
//
// Used with triangle-list, line-strip and line-list pipelines.

struct PxUBO {
    canvasWH : vec2f,
    color    : vec4f,
};

@group(0) @binding(0) var<uniform> U : PxUBO;
@group(0) @binding(1) var<storage, read> pxPts : array<vec2f>;
#ifdef VERTEX_COLOR
@group(0) @binding(2) var<storage, read> pxCol : array<vec4f>;
#endif

fn pxToNdc(p : vec2f) -> vec2f {
    return vec2f(p.x / U.canvasWH.x * 2.0 - 1.0,
                 1.0 - p.y / U.canvasWH.y * 2.0);
}

struct VSOut {
    @builtin(position) pos : vec4f,
    @location(0) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    var o : VSOut;
    o.pos = vec4f(pxToNdc(pxPts[vi]), 0.0, 1.0);
#ifdef VERTEX_COLOR
    o.color = pxCol[vi];
#else
    o.color = U.color;
#endif
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
