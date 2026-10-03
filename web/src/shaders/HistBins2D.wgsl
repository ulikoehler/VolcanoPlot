// HistBins2D.wgsl — 2-D histogram accumulation (mpl hist2d).
//
// One invocation per sample; atomicAdd into a row-major count grid.
// Uniform edges only (the auto/Fixed binning modes); non-uniform edges
// stay on the CPU path.
//
// Bindings:
//   0 uniform PHistBins2D
//   1 storage readonly xy  : array<vec2f>   (x,y interleaved)
//   2 storage rw       bins: array<atomic<u32>>

struct Bins2DPC {
    n : u32, nBinsX : u32, nBinsY : u32, pad : u32,
    x0 : f32, invWX : f32, y0 : f32, invWY : f32,
};

@group(0) @binding(0) var<uniform> pc : Bins2DPC;
@group(0) @binding(1) var<storage, read> xy : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> bins : array<atomic<u32>>;

@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.n) { return; }
    let p = xy[i];
    if (p.x != p.x || p.y != p.y) { return; }
    let fx = (p.x - pc.x0) * pc.invWX;
    let fy = (p.y - pc.y0) * pc.invWY;
    if (fx < 0.0 || fy < 0.0) { return; }
    var bx = u32(fx);
    var by = u32(fy);
    // Last bin is closed on the right, like the CPU loop.
    if (bx >= pc.nBinsX) {
        if (bx > pc.nBinsX) { return; }
        bx = pc.nBinsX - 1u;
    }
    if (by >= pc.nBinsY) {
        if (by > pc.nBinsY) { return; }
        by = pc.nBinsY - 1u;
    }
    atomicAdd(&bins[by * pc.nBinsX + bx], 1u);
}
