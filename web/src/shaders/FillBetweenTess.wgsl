// web/src/shaders/FillBetweenTess.wgsl — fill_between band tessellation
// (op 57). One invocation per input segment [i, i+1]. Emits exactly what
// buildFillBetweenTriangles does on the CPU:
//
//   * both ends masked in  → the trapezoid quad of the segment;
//   * interpolate && the run ends here → the boundary triangle from the
//     f1 == f2 crossing on this segment back to the previous point.
//
// Runs shorter than two points fill nothing (mpl's where[i] &&
// where[i+1] rule), and the crossing points are the same
// FillBetweenPolyCollection._get_interpolating_points values.
//
//   0 uniform pc { n, flags, maxVerts, pad, bx, ax, by, ay, color }
//   1 storage x    : array<f32>
//   2 storage y1   : array<f32>
//   3 storage y2   : array<f32>
//   4 storage mask : array<u32>   (0/1 per point)
//   5 storage soup : array<f32>   ({vec2 pos_px, vec4 rgba})
//   6 storage args : array<atomic<u32>> — args[0] = vertex count
//
// flags bit0 = interpolate.

struct FbPC {
    n : u32, flags : u32, maxVerts : u32, pad0 : u32,
    bx : f32, ax : f32, by : f32, ay : f32,
    color : vec4f,
};

@group(0) @binding(0) var<uniform> pc : FbPC;
@group(0) @binding(1) var<storage, read> xs : array<f32>;
@group(0) @binding(2) var<storage, read> ys1 : array<f32>;
@group(0) @binding(3) var<storage, read> ys2 : array<f32>;
@group(0) @binding(4) var<storage, read> mask : array<u32>;
@group(0) @binding(5) var<storage, read_write> soup : array<f32>;
@group(0) @binding(6) var<storage, read_write> args : array<atomic<u32>>;

fn toPx(p : vec2f) -> vec2f {
    return vec2f(pc.bx + p.x * pc.ax, pc.by + p.y * pc.ay);
}

fn emitVert(idx : u32, p : vec2f) {
    let b = idx * 6u;
    soup[b]      = p.x; soup[b + 1u] = p.y;
    soup[b + 2u] = pc.color.r; soup[b + 3u] = pc.color.g;
    soup[b + 4u] = pc.color.b; soup[b + 5u] = pc.color.a;
}

fn emitTri(a : vec2f, b : vec2f, c : vec2f) {
    let base = atomicAdd(&args[0], 3u);
    if (base + 3u > pc.maxVerts) { return; }
    emitVert(base, a); emitVert(base + 1u, b); emitVert(base + 2u, c);
}

fn emitQuad(a : vec2f, b : vec2f, c : vec2f, d : vec2f) {
    let base = atomicAdd(&args[0], 6u);
    if (base + 6u > pc.maxVerts) { return; }
    emitVert(base, a); emitVert(base + 1u, b); emitVert(base + 2u, c);
    emitVert(base + 3u, b); emitVert(base + 4u, d); emitVert(base + 5u, c);
}

/// f1 == f2 crossing inside segment [i-1, i], as the (x, f1) point on
/// the upper curve (mirrors plot::crossingPoint).
fn crossing(i : u32) -> vec2f {
    let im1 = i - 1u;
    let d0 = ys1[im1] - ys2[im1];
    let d1 = ys1[i] - ys2[i];
    var tc = xs[im1];
    let den = d0 - d1;
    if (abs(den) > 1e-20) {
        tc = xs[im1] + (d0 / den) * (xs[i] - xs[im1]);
    }
    // Clamped linear interpolation of y1 at tc (the CPU uses np.interp
    // with clamped ends).
    var w = 0.0;
    if (xs[i] != xs[im1]) {
        w = clamp((tc - xs[im1]) / (xs[i] - xs[im1]), 0.0, 1.0);
    }
    let fc = ys1[im1] + w * (ys1[i] - ys1[im1]);
    return vec2f(tc, fc);
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i + 1u >= pc.n) { return; }
    let j = i + 1u;
    if (mask[i] == 0u) { return; }
    let interpolate = (pc.flags & 1u) != 0u;
    let prevIn = i > 0u && mask[i - 1u] != 0u;

    if (mask[j] != 0u) {
        // Interior segment — the trapezoid, with the left edge trimmed
        // to the run's start crossing.
        var ul = vec2f(xs[i], ys1[i]);
        var ll = vec2f(xs[i], ys2[i]);
        if (interpolate && !prevIn && i > 0u) {
            let cr = crossing(i);
            ul = cr; ll = cr;
        }
        emitQuad(toPx(ul), toPx(vec2f(xs[j], ys1[j])),
                 toPx(ll), toPx(vec2f(xs[j], ys2[j])));
        return;
    }
    // Run ends at i: only a run of at least two points fills, and only
    // with interpolate does the closing triangle appear.
    if (!interpolate || !prevIn) { return; }
    let cr = crossing(j);
    emitTri(toPx(cr), toPx(vec2f(xs[i], ys1[i])),
            toPx(vec2f(xs[i], ys2[i])));
}
