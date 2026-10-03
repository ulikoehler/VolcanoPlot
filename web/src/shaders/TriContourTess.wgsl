// web/src/shaders/TriContourTess.wgsl — marching triangles for the
// tricontour family (op 53). One invocation per (triangle, level) —
// mode 0 strokes each crossing segment exactly like ContourTess
// (per-level colour, per-level dash pattern restarted per segment);
// mode 1 clips the triangle to the band [level_i, level_{i+1}] via
// Sutherland–Hodgman and fans it into fill triangles.
//
//   0 uniform pc
//   1 storage xyz      : array<f32>     (x, y, z per point — packed
//     12-byte triples; a storage array<vec3f> would have 16-byte
//     stride and misread the packed data)
//   2 storage tris     : array<u32>     (3 indices / triangle)
//   3 storage levels   : array<f32>
//   4 storage colors   : array<vec4f>   (per level / per band)
//   5 storage dashes   : array<vec2f>   ((on, off); on==0 → solid)
//   6 storage soup     : array<f32>     ({vec2 pos_px, vec4 rgba} verts)
//   7 storage args     : array<atomic<u32>> — args[0] = vertex count

struct TriPC {
    nTris : u32, nLevels : u32, mode : u32, pad0 : u32,
    bx : f32, ax : f32, by : f32, ay : f32,
    hwidth : f32, pad1 : f32, pad2 : f32, pad3 : u32,
    maxVerts : u32, dashMul : u32, pad4 : u32, pad5 : u32,
};

@group(0) @binding(0) var<uniform> pc : TriPC;
@group(0) @binding(1) var<storage, read> xyz : array<f32>;

fn pt3(i : u32) -> vec3f {
    return vec3f(xyz[i * 3u], xyz[i * 3u + 1u], xyz[i * 3u + 2u]);
}
@group(0) @binding(2) var<storage, read> tris : array<u32>;
@group(0) @binding(3) var<storage, read> levels : array<f32>;
@group(0) @binding(4) var<storage, read> colors : array<vec4f>;
@group(0) @binding(5) var<storage, read> dashes : array<vec2f>;
@group(0) @binding(6) var<storage, read_write> soup : array<f32>;
@group(0) @binding(7) var<storage, read_write> args : array<atomic<u32>>;

fn toPx(p : vec2f) -> vec2f {
    return vec2f(pc.bx + p.x * pc.ax, pc.by + p.y * pc.ay);
}

fn interpEdge(p0 : vec3f, p1 : vec3f, level : f32) -> vec2f {
    let denom = p1.z - p0.z;
    if (abs(denom) < 1e-20) { return (p0.xy + p1.xy) * 0.5; }
    let t = (level - p0.z) / denom;
    return p0.xy + t * (p1.xy - p0.xy);
}

fn emitVert(idx : u32, p : vec2f, c : vec4f) {
    let b = idx * 6u;
    soup[b]      = p.x; soup[b + 1u] = p.y;
    soup[b + 2u] = c.r; soup[b + 3u] = c.g;
    soup[b + 4u] = c.b; soup[b + 5u] = c.a;
}

/// One stroked quad a→b (positions already in pixel space).
fn emitQuad(a : vec2f, b : vec2f, c : vec4f) -> bool {
    var d = b - a;
    let len = length(d);
    if (len < 1e-6) { return true; }
    d = d / len;
    let n = vec2f(-d.y, d.x) * pc.hwidth;
    let base = atomicAdd(&args[0], 6u);
    if (base + 6u > pc.maxVerts) { return false; }
    emitVert(base,      a - n, c);
    emitVert(base + 1u, a + n, c);
    emitVert(base + 2u, b + n, c);
    emitVert(base + 3u, a - n, c);
    emitVert(base + 4u, b + n, c);
    emitVert(base + 5u, b - n, c);
    return true;
}

fn emitTri(a : vec2f, b : vec2f, c : vec2f, col : vec4f) -> bool {
    let base = atomicAdd(&args[0], 3u);
    if (base + 3u > pc.maxVerts) { return false; }
    emitVert(base,      a, col);
    emitVert(base + 1u, b, col);
    emitVert(base + 2u, c, col);
    return true;
}

// ── mode 0: isolines ────────────────────────────────────────────────
fn linesFor(t : u32, li : u32) {
    let i0 = tris[t * 3u]; let i1 = tris[t * 3u + 1u];
    let i2 = tris[t * 3u + 2u];
    let p0 = pt3(i0); let p1 = pt3(i1); let p2 = pt3(i2);
    if (!(p0.z == p0.z && p1.z == p1.z && p2.z == p2.z)) { return; }
    let level = levels[li];
    let a0 = p0.z >= level; let a1 = p1.z >= level; let a2 = p2.z >= level;
    let above = u32(a0) + u32(a1) + u32(a2);
    if (above == 0u || above == 3u) { return; }
    var cr : array<vec2f, 2>;
    var ci = 0u;
    if (a0 != a1) { cr[ci] = interpEdge(p0, p1, level); ci = ci + 1u; }
    if (a1 != a2) { cr[ci] = interpEdge(p1, p2, level); ci = ci + 1u; }
    if (a2 != a0 && ci < 2u) { cr[ci] = interpEdge(p2, p0, level);
                              ci = ci + 1u; }
    if (ci != 2u) { return; }
    let a = toPx(cr[0]);
    let b = toPx(cr[1]);
    let c = colors[li];
    let dash = dashes[li];
    if (dash.x <= 0.0) {
        let ok = emitQuad(a, b, c);
        if (!ok) { return; }
        return;
    }
    // Dashed level — the CPU path dashes per segment.
    var d = b - a;
    let len = length(d);
    if (len < 1e-6) { return; }
    d = d / len;
    let period = dash.x + dash.y;
    var s = 0.0;
    var k = 0u;
    loop {
        if (s >= len || k >= pc.dashMul) { break; }
        let sEnd = min(s + dash.x, len);
        let ok = emitQuad(a + d * s, a + d * sEnd, c);
        if (!ok) { break; }
        s = s + period;
        k = k + 1u;
    }
}

// ── mode 1: filled bands ─────────────────────────────────────────────
fn fillFor(t : u32, bi : u32) {
    let i0 = tris[t * 3u]; let i1 = tris[t * 3u + 1u];
    let i2 = tris[t * 3u + 2u];
    let p0 = pt3(i0); let p1 = pt3(i1); let p2 = pt3(i2);
    if (!(p0.z == p0.z && p1.z == p1.z && p2.z == p2.z)) { return; }
    let lo = levels[bi];
    let hi = levels[bi + 1u];
    // Sutherland–Hodgman clip of the triangle against z >= lo then
    // z <= hi — vertices carry (xy, z) so the half-plane test is a
    // scalar compare.
    var poly : array<vec3f, 8>;
    poly[0] = p0; poly[1] = p1; poly[2] = p2;
    var n = 3u;
    for (var clip = 0u; clip < 2u; clip = clip + 1u) {
        let bound = select(hi, lo, clip == 0u);
        let keepLo = clip == 0u;
        var out : array<vec3f, 8>;
        var m = 0u;
        for (var j = 0u; j < n; j = j + 1u) {
            let k = (j + 1u) % n;
            let pj = poly[j]; let pk = poly[k];
            let aj = select(pj.z <= bound, pj.z >= bound, keepLo);
            let ak = select(pk.z <= bound, pk.z >= bound, keepLo);
            if (aj) { out[m] = pj; m = m + 1u; }
            if (aj != ak) {
                let tt = (bound - pj.z) / (pk.z - pj.z);
                out[m] = vec3f(pj.xy + tt * (pk.xy - pj.xy), bound);
                m = m + 1u;
            }
        }
        n = m;
        for (var j = 0u; j < m; j = j + 1u) { poly[j] = out[j]; }
    }
    if (n < 3u) { return; }
    let col = colors[bi];
    // Fan triangulation.
    for (var j = 1u; j + 1u < n; j = j + 1u) {
        let ok = emitTri(toPx(poly[0].xy), toPx(poly[j].xy),
                         toPx(poly[j + 1u].xy), col);
        if (!ok) { return; }
    }
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let id = gid.x;
    if (id >= pc.nTris * pc.nLevels) { return; }
    let t = id / pc.nLevels;
    let li = id % pc.nLevels;
    if (pc.mode == 0u) { linesFor(t, li); return; }
    fillFor(t, li);
}
