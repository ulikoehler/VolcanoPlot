// web/src/shaders/QuiverTess.wgsl — quiver arrowhead expansion (op 58).
// One invocation per arrow: reads a pixel-space shaft segment
// (x0, y0, x1, y1), expands the head polygon into the indirect
// triangle soup — same contract as ContourTess/TriContourTess.
//
//   0 uniform pc : { n, mode, hw2, hl, hal, maxVerts, pad, pad, color }
//   1 storage segs  : array<f32>  (4 floats per arrow, pixel space)
//   2 storage soup  : array<f32>  ({vec2 pos_px, vec4 rgba} verts)
//   3 storage args  : array<atomic<u32>> — args[0] = vertex count
//
// mode 0 = simple triangle head (tip, base1, base2); mode 1 = mpl
// notched head (tip, base1, axis | tip, axis, base2 — 6 verts).

struct QuiverPC {
    n : u32, mode : u32, maxVerts : u32, pad0 : u32,
    hw2 : f32, hl : f32, hal : f32, pad1 : f32,
    color : vec4f,
};

@group(0) @binding(0) var<uniform> pc : QuiverPC;
@group(0) @binding(1) var<storage, read> segs : array<f32>;
@group(0) @binding(2) var<storage, read_write> soup : array<f32>;
@group(0) @binding(3) var<storage, read_write> args : array<atomic<u32>>;

fn emitVert(idx : u32, p : vec2f) {
    let b = idx * 6u;
    soup[b]      = p.x; soup[b + 1u] = p.y;
    soup[b + 2u] = pc.color.r; soup[b + 3u] = pc.color.g;
    soup[b + 4u] = pc.color.b; soup[b + 5u] = pc.color.a;
}

fn emitTri(a : vec2f, b : vec2f, c : vec2f) -> bool {
    let base = atomicAdd(&args[0], 3u);
    if (base + 3u > pc.maxVerts) { return false; }
    emitVert(base,      a);
    emitVert(base + 1u, b);
    emitVert(base + 2u, c);
    return true;
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.n) { return; }
    let s0 = vec2f(segs[i * 4u],      segs[i * 4u + 1u]);
    let s1 = vec2f(segs[i * 4u + 2u], segs[i * 4u + 3u]);
    var d = s1 - s0;
    let len = length(d);
    if (len < 1.0) { return; }          // too short for a head (CPU rule)
    d = d / len;
    let perp = vec2f(-d.y, d.x);
    let tip = s1;
    let base1 = s1 - d * pc.hl + perp * pc.hw2;
    let base2 = s1 - d * pc.hl - perp * pc.hw2;
    if (pc.mode == 0u) {
        emitTri(tip, base1, base2);
        return;
    }
    // mpl notched head: a notch vertex on the shaft axis at hal back.
    let axis = s1 - d * pc.hal;
    if (!emitTri(tip, base1, axis)) { return; }
    emitTri(tip, axis, base2);
}
