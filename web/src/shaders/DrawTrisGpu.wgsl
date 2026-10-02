// web/src/shaders/DrawTrisGpu.wgsl — pre-tessellated pixel-space mesh
// (GpuLineRenderer output / SpineRenderer::drawTrianglesGpu port).
// Vertex record: vec2f pos_px + vec4f color = 24 B.
//
//   0 uniform { canvasWH : vec2f, byteOff : u32, pad : u32 }
//   1 storage verts : array<u32>  (raw view; record = 6 u32)

struct GpuUBO {
    canvasWH : vec2f,
    byteOff  : u32,
    pad      : u32,
};

@group(0) @binding(0) var<uniform> U : GpuUBO;
@group(0) @binding(1) var<storage, read> raw : array<u32>;

fn w2f(w : u32) -> f32 { return bitcast<f32>(w); }

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    let base = U.byteOff / 4u + vi * 6u;
    let px = vec2f(w2f(raw[base]), w2f(raw[base + 1u]));
    let col = vec4f(w2f(raw[base + 2u]), w2f(raw[base + 3u]),
                    w2f(raw[base + 4u]), w2f(raw[base + 5u]));
    var o : VSOut;
    o.p = vec4f(px.x / U.canvasWH.x * 2.0 - 1.0,
                1.0 - px.y / U.canvasWH.y * 2.0, 0.0, 1.0);
    o.color = col;
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
