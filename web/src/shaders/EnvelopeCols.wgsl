// web/src/shaders/EnvelopeCols.wgsl — per-pixel-column min/max envelope
// (WGSL port of GpuLineRendererVk's kEnvelopeGlsl). One invocation per
// segment (i-1, i): the segment's y-range is deposited in every pixel
// column it crosses, with linear interpolation at column boundaries —
// identical output to plot::envelopeDecimateData, on unsorted x.
//
//   0 uniform { ax, kx, cx0, cx1, n, pad[3] }
//   1 storage pts : array<vec2f>          (read)
//   2 storage out : array<atomic<u32>>    — [0..n) mn keys, [n..2n) mx
//     keys (seeded ord(+inf) / ord(-inf) by the host).

struct EnvU {
    ax : f32, kx : f32,
    cx0 : i32, cx1 : i32,
    n : u32, nCols : u32, pad1 : u32, pad2 : u32,
};
@group(0) @binding(0) var<uniform> U : EnvU;
@group(0) @binding(1) var<storage, read> pts : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> out_ : array<atomic<u32>>;

// Order-preserving f32→u32 map for atomics (same as ReduceMinMax).
fn ord(f : f32) -> u32 {
    let u = bitcast<u32>(f);
    return select(u | 0x80000000u, ~u, (u & 0x80000000u) != 0u);
}

fn upd(c : i32, lo : f32, hi : f32) {
    let i = c - U.cx0;
    if (i < 0 || i >= U.cx1 - U.cx0 + 1) { return; }
    atomicMin(&out_[u32(i)], ord(lo));
    atomicMax(&out_[u32(i) + U.nCols], ord(hi));
}

fn finite2(v : vec2f) -> bool {
    // Rejects NaN and ±inf without an isinf builtin: NaN*0=NaN,
    // ±inf*0=NaN, finite*0=0.
    return v.x * 0.0 == 0.0 && v.y * 0.0 == 0.0;
}

@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i == 0u || i >= U.n) { return; }
    let a = pts[i - 1u];
    let b = pts[i];
    if (!finite2(a) || !finite2(b)) { return; }
    let xa = U.ax + U.kx * a.x;
    let xb = U.ax + U.kx * b.x;
    let xlo = min(xa, xb);
    let xhi = max(xa, xb);
    var s = max(U.cx0, i32(floor(xlo)));
    var e = min(U.cx1, i32(floor(xhi)));
    if (s > e) { return; }
    if (s == e) {
        upd(s, min(a.y, b.y), max(a.y, b.y));
        return;
    }
    for (var c = s; c <= e; c = c + 1) {
        var lo : f32; var hi : f32;
        if (xhi - xlo < 1e-6) {
            lo = min(a.y, b.y);
            hi = max(a.y, b.y);
        } else {
            let xl = max(xlo, f32(c));
            let xr = min(xhi, f32(c + 1));
            let t0 = (xl - xa) / (xb - xa);
            let t1 = (xr - xa) / (xb - xa);
            let yl = a.y + (b.y - a.y) * t0;
            let yr = a.y + (b.y - a.y) * t1;
            lo = min(yl, yr);
            hi = max(yl, yr);
        }
        upd(c, lo, hi);
    }
}
