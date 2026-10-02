// web/src/shaders/KdeEval2D.wgsl — 2D Gaussian KDE over a grid.
// One thread per output cell; each gathers all samples.
// Port of KdeEvalRenderer.cpp kKdeGlsl.

struct PC {
    nSamples : u32, gridW : u32, gridH : u32, mailbox : u32,
    xMin : f32, xStep : f32, yMin : f32, yStep : f32,
    inv2bwX2 : f32, inv2bwY2 : f32, norm : f32, pad : f32,
}
@group(0) @binding(0) var<uniform> pc : PC;
@group(0) @binding(1) var<storage, read> samples : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> gridOut : array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) g : vec3u) {
    let i = g.x; let j = g.y;
    if (i >= pc.gridW || j >= pc.gridH) { return; }
    let gx = pc.xMin + (f32(i) + 0.5) * pc.xStep;
    let gy = pc.yMin + (f32(j) + 0.5) * pc.yStep;
    var sum = 0.0;
    for (var k = 0u; k < pc.nSamples; k++) {
        let s = samples[k];
        let dx = gx - s.x;
        let dy = gy - s.y;
        sum += exp(-(dx * dx * pc.inv2bwX2 + dy * dy * pc.inv2bwY2));
    }
    gridOut[j * pc.gridW + i] = sum * pc.norm;
}
