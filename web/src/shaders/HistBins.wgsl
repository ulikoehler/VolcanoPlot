// HistBins.wgsl — 1-D histogram accumulation.
//
// One invocation per sample; atomicAdd into the count vector. Uniform
// edges: bin = floor((v - e0) * invW).
//
// Bindings:
//   0 uniform PHistBins
//   1 storage readonly data : array<f32>
//   2 storage rw       bins : array<atomic<u32>>

struct BinsPC {
    n : u32, nBins : u32, pad0 : u32, pad1 : u32,
    e0 : f32, invW : f32, pad2 : f32, pad3 : f32,
};

@group(0) @binding(0) var<uniform> pc : BinsPC;
@group(0) @binding(1) var<storage, read> data : array<f32>;
@group(0) @binding(2) var<storage, read_write> bins : array<atomic<u32>>;

@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.n) { return; }
    let v = data[i];
    if (v != v) { return; }
    let f = (v - pc.e0) * pc.invW;
    if (f < 0.0) { return; }
    var b = u32(f);
    if (b >= pc.nBins) {
        if (b > pc.nBins) { return; }
        b = pc.nBins - 1u;
    }
    atomicAdd(&bins[b], 1u);
}
