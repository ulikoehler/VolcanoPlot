// web/src/shaders/DrawInstanced.wgsl — InstancedPathRenderer port.
// Template triangle soup (vec2 px-space unit template) × per-instance
// {ox, oy, sx, sy, r, g, b, a} records (8×f32 = 32 B).
//
//   0 uniform { canvasWH : vec2f, pad : vec2f }
//   1 storage tpl  : array<vec2f>
//   2 storage inst : array<f32>   (8 per instance)

struct InstUBO { canvasWH : vec2f, pad : vec2f };

@group(0) @binding(0) var<uniform> U : InstUBO;
@group(0) @binding(1) var<storage, read> tpl : array<vec2f>;
@group(0) @binding(2) var<storage, read> inst : array<f32>;

struct VSOut {
    @builtin(position) p : vec4f,
    @location(0) color : vec4f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32,
              @builtin(instance_index) ii : u32) -> VSOut {
    let b = ii * 8u;
    let org = vec2f(inst[b], inst[b + 1u]);
    let sc  = vec2f(inst[b + 2u], inst[b + 3u]);
    let col = vec4f(inst[b + 4u], inst[b + 5u], inst[b + 6u],
                    inst[b + 7u]);
    let px = tpl[vi] * sc + org;
    var o : VSOut;
    o.p = vec4f(px.x / U.canvasWH.x * 2.0 - 1.0,
                1.0 - px.y / U.canvasWH.y * 2.0, 0.0, 1.0);
    o.color = col;
    return o;
}

@fragment fn fs(v : VSOut) -> @location(0) vec4f {
    return v.color;
}
