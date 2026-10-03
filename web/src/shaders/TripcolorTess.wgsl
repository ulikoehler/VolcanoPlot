// web/src/shaders/TripcolorTess.wgsl — tripcolor expansion (op 61).
// One invocation per triangle: map the scalar field through the
// 259-entry colormap LUT (same indexing as PcmTess), apply the
// data→pixel affine, and emit {vec2 pos_px, vec4 rgba} soup verts with
// an atomic counter — the same indirect-draw contract as ContourTess.
//
// mode 0: face color   — zBuf holds one value per triangle
// mode 1: flat         — face color = avg of the three vertex values
// mode 2: gouraud      — per-vertex colors
//
//   0 uniform pc { nTris, mode, maxVerts, pad, bx, ax, by, ay }
//   1 storage xy   : array<f32>  (x, y pairs, data space)
//   2 storage tris : array<u32>  (3 point indices per triangle)
//   3 storage z    : array<f32>  (per-point, or per-face in mode 0)
//   4 storage lut  : array<vec4f> (259 entries)
//   5 storage soup : array<f32>  (24 B per vert: pos, rgba)
//   6 storage cnt  : array<atomic<u32>>

struct TcPC {
    nTris : u32, mode : u32, maxVerts : u32, pad : u32,
    bx : f32, ax : f32, by : f32, ay : f32,
};

@group(0) @binding(0) var<uniform> pc : TcPC;
@group(0) @binding(1) var<storage, read> xy : array<f32>;
@group(0) @binding(2) var<storage, read> tris : array<u32>;
@group(0) @binding(3) var<storage, read> z : array<f32>;
@group(0) @binding(4) var<storage, read> lut : array<vec4f>;
@group(0) @binding(5) var<storage, read_write> soup : array<f32>;
@group(0) @binding(6) var<storage, read_write> cnt : array<atomic<u32>>;

fn colorOf(t : f32) -> vec4f {
    if (t != t) { return lut[258]; }              // NaN → bad
    if (t < 0.0) { return lut[256]; }             // under
    if (t > 1.0) { return lut[257]; }             // over
    return lut[u32(t * 255.0 + 0.5)];
}

fn px(p : vec2f) -> vec2f {
    return vec2f(pc.bx + p.x * pc.ax, pc.by + p.y * pc.ay);
}

fn emitVert(v : u32, p : vec2f, c : vec4f) {
    let b = v * 6u;
    soup[b] = p.x;      soup[b + 1u] = p.y;
    soup[b + 2u] = c.r; soup[b + 3u] = c.g;
    soup[b + 4u] = c.b; soup[b + 5u] = c.a;
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let t = gid.x;
    if (t >= pc.nTris) { return; }
    let i0 = tris[t * 3u];
    let i1 = tris[t * 3u + 1u];
    let i2 = tris[t * 3u + 2u];

    var c0 : vec4f; var c1 : vec4f; var c2 : vec4f;
    if (pc.mode == 0u) {
        let fc = colorOf(z[t]);
        c0 = fc; c1 = fc; c2 = fc;
    } else if (pc.mode == 1u) {
        let fc = colorOf((z[i0] + z[i1] + z[i2]) / 3.0);
        c0 = fc; c1 = fc; c2 = fc;
    } else {
        c0 = colorOf(z[i0]);
        c1 = colorOf(z[i1]);
        c2 = colorOf(z[i2]);
    }
    // CPU rule: drop fully transparent faces.
    if (pc.mode == 2u) {
        if (c0.a == 0.0 && c1.a == 0.0 && c2.a == 0.0) { return; }
    } else if (c0.a == 0.0) {
        return;
    }

    let base = atomicAdd(&cnt[0], 3u);
    if (base + 3u > pc.maxVerts) { return; }
    emitVert(base,      px(vec2f(xy[i0 * 2u], xy[i0 * 2u + 1u])), c0);
    emitVert(base + 1u, px(vec2f(xy[i1 * 2u], xy[i1 * 2u + 1u])), c1);
    emitVert(base + 2u, px(vec2f(xy[i2 * 2u], xy[i2 * 2u + 1u])), c2);
}
