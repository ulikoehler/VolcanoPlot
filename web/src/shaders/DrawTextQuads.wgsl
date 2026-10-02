// web/src/shaders/DrawTextQuads.wgsl — TextRenderer port.
// glyb emits triangle-soup quads: 32 B records {pos vec2f, uv vec2f,
// color vec4f}, pixel space. Atlas is r8unorm.
//
//   0 uniform { canvasWH : vec2f, pad : vec2f }
//   1 storage quads : array<u32>   (8 u32 per vertex)
//   2 texture atlas : texture_2d<f32>
//   3 sampler samp  : sampler (linear)

struct TextUBO { canvasWH : vec2f, pad : vec2f };

@group(0) @binding(0) var<uniform> U : TextUBO;
@group(0) @binding(1) var<storage, read> raw : array<u32>;
@group(0) @binding(2) var atlas : texture_2d<f32>;
@group(0) @binding(3) var samp : sampler;

fn w2f(w : u32) -> f32 { return bitcast<f32>(w); }

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) uv : vec2f,
    @location(1) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VSOut {
    let b = vi * 8u;
    let px = vec2f(w2f(raw[b]), w2f(raw[b + 1u]));
    var o : VSOut;
    o.p = vec4f(px.x / U.canvasWH.x * 2.0 - 1.0,
                1.0 - px.y / U.canvasWH.y * 2.0, 0.0, 1.0);
    o.uv = vec2f(w2f(raw[b + 2u]), w2f(raw[b + 3u]));
    o.color = vec4f(w2f(raw[b + 4u]), w2f(raw[b + 5u]),
                    w2f(raw[b + 6u]), w2f(raw[b + 7u]));
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    let a = textureSample(atlas, samp, v.uv).r;
    return vec4f(v.color.rgb, v.color.a * a);
}
