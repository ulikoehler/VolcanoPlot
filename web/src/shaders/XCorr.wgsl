// web/src/shaders/XCorr.wgsl — cross/autocorrelation (op 59). One
// invocation per lag index k in [0, 2*maxLag]: c[k] = Σ_i x[i]·y[i-lag]
// for lag = k - maxLag (np.correlate "full" restricted to ±maxLag).
// flags bit0 = normed — the result is multiplied by the host-computed
// invNorm. The CPU path accumulates in f64; the device sums in f32, so
// values can drift slightly at large n (opt-in only).
//
//   0 uniform pc { n, maxLag, flags, invNorm }
//   1 storage x     : array<f32>
//   2 storage y     : array<f32>
//   3 storage outv  : array<f32>  (2*maxLag+1)

struct XcPC {
    n : u32, maxLag : u32, flags : u32, invNorm : f32,
};

@group(0) @binding(0) var<uniform> pc : XcPC;
@group(0) @binding(1) var<storage, read> xs : array<f32>;
@group(0) @binding(2) var<storage, read> ys : array<f32>;
@group(0) @binding(3) var<storage, read_write> outv : array<f32>;

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let k = gid.x;
    if (k > pc.maxLag * 2u) { return; }
    let lag = i32(k) - i32(pc.maxLag);
    var sum = 0.0;
    for (var i = 0u; i < pc.n; i = i + 1u) {
        let j = i32(i) - lag;
        if (j >= 0 && j < i32(pc.n)) {
            sum = sum + xs[i] * ys[u32(j)];
        }
    }
    if ((pc.flags & 1u) != 0u) { sum = sum * pc.invNorm; }
    outv[k] = sum;
}
