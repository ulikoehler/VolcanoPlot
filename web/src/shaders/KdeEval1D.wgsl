// web/src/shaders/KdeEval1D.wgsl — 1-D Gaussian KDE.
// Port of VulkanGpuServices.cpp kKde1dGlsl: one thread per eval point.

struct PC {
    ns : u32, ne : u32, lo : f32, step : f32, bw : f32, pad : vec3f,
}
@group(0) @binding(0) var<uniform> pc : PC;
@group(0) @binding(1) var<storage, read> src : array<f32>;
@group(0) @binding(2) var<storage, read_write> dst : array<f32>;

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) g : vec3u) {
    let i = g.x;
    if (i >= pc.ne) { return; }
    let y = pc.lo + f32(i) * pc.step;
    var sum = 0.0;
    for (var s = 0u; s < pc.ns; s++) {
        let t = (y - src[s]) / pc.bw;
        sum += exp(-0.5 * t * t) * 0.39894228;
    }
    dst[i] = sum / (f32(pc.ns) * pc.bw);
}
