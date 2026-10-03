// web/src/shaders/PolyFillMask.wgsl — GPU even-odd scanline polygon fill.
//
// Two entry points, dispatched in sequence by the interpreter:
//
//   edges — one invocation per edge (each ring's closing segment
//           included): for every scan row the edge spans, bump the
//           crossing counter at the column it crosses. Parity then
//           accumulates to the right of each crossing.
//   scan  — one invocation per row: walk the row accumulating crossing
//           parity and write the fill colour where it is odd.
//
// The result is an rgba8 coverage image the interpreter blits into the
// texture the image draw samples in rgba mode — the same fill rule mpl
// applies to paths, so holes and nested rings resolve by parity.

struct U {
    W          : u32,
    H          : u32,
    nPts       : u32,
    nRings     : u32,
    color      : vec4f,     // fill colour, straight (non-premultiplied)
    rowStride  : u32,       // mask row stride in BYTES (copy alignment)
};

@group(0) @binding(0) var<uniform> u : U;
@group(0) @binding(1) var<storage, read> pts : array<f32>;      // vec2 ring pts
@group(0) @binding(2) var<storage, read> offs : array<u32>;     // ring offsets
@group(0) @binding(3) var<storage, read_write> delta : array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> mask : array<u32>;

/// Ring index owning edge `e` — rings are few, a linear walk is fine.
fn ringOf(e : u32) -> u32 {
    var r : u32 = 0u;
    loop {
        if (r + 1u >= u.nRings || e < offs[r + 1u]) { return r; }
        r += 1u;
    }
}

@compute @workgroup_size(64)
fn edges(@builtin(global_invocation_id) gid : vec3u) {
    // Edge e joins ring point e to the next point of the same ring.
    let e = gid.x;
    if (e >= u.nPts) { return; }
    let r = ringOf(e);
    let lo = offs[r];
    let hi = offs[r + 1u];
    if (hi - lo < 3u) { return; }
    let j = select(e + 1u, lo, e + 1u >= hi);
    let ax = pts[e * 2u];
    let ay = pts[e * 2u + 1u];
    let bx = pts[j * 2u];
    let by = pts[j * 2u + 1u];
    if (ay == by) { return; }                    // horizontal — no crossing
    let dy = by - ay;
    // Rows whose centre line the edge spans (half-open, so a vertex on
    // the line counts once).
    let yLo = min(ay, by);
    let yHi = max(ay, by);
    var y = i32(ceil(yLo - 0.5));
    if (f32(y) + 0.5 < yLo) { y += 1; }
    let yEnd = i32(ceil(yHi - 0.5));
    loop {
        if (y >= yEnd || y >= i32(u.H)) { break; }
        if (y >= 0) {
            let t = (f32(y) + 0.5 - ay) / dy;
            let x = ax + t * (bx - ax);
            let c = i32(floor(x));
            if (c >= 0 && c < i32(u.W)) {
                atomicAdd(&delta[u32(y) * u.W + u32(c)], 1u);
            }
        }
        y += 1;
    }
}

@compute @workgroup_size(64)
fn scan(@builtin(global_invocation_id) gid : vec3u) {
    let y = gid.x;
    if (y >= u.H) { return; }
    // Straight (non-premultiplied) colour bytes, little-endian.
    let cr = u32(clamp(u.color.r, 0.0, 1.0) * 255.0 + 0.5);
    let cg = u32(clamp(u.color.g, 0.0, 1.0) * 255.0 + 0.5);
    let cb = u32(clamp(u.color.b, 0.0, 1.0) * 255.0 + 0.5);
    let ca = u32(clamp(u.color.a, 0.0, 1.0) * 255.0 + 0.5);
    let packed = cr | (cg << 8u) | (cb << 16u) | (ca << 24u);
    var parity : u32 = 0u;
    for (var x : u32 = 0u; x < u.W; x += 1u) {
        parity += atomicLoad(&delta[y * u.W + x]);
        // Only the covered texels carry colour — the rest stay
        // transparent so the image draw composites the fill shape.
        mask[y * (u.rowStride >> 2u) + x] =
            select(0u, packed, (parity & 1u) == 1u);
    }
}
