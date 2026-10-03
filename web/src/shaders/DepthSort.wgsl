// web/src/shaders/DepthSort.wgsl — painter's-order 3D triangle sort
// (op 54). Two stages, dispatched by the interpreter:
//
//   `keygen`  — one invocation per triangle: the mean clip depth of its
//               three vertices (projectDepth3D averaged, the CPU rule)
//               becomes the sort key; the index buffer is seeded with
//               the identity permutation. Padding entries (up to the
//               next power of two) get -inf so they sort last.
//   `bitonic` — one compare-exchange step (k, j from the uniform),
//               swapping three index entries and the key together, so
//               the index buffer itself ends up back-to-front and can
//               be drawn directly.
//
//   0 uniform pc { nTris, nPad, k, j, vp[16] }  (80 B)
//   1 storage pos  : array<u32>  (packed vec3f — 3 u32 per vertex)
//   2 storage idx  : array<u32>  (3 per triangle)
//   3 storage keys : array<f32>  (one per triangle)

struct DsPC {
    nTris : u32, nPad : u32, k : u32, j : u32,
    vp : array<vec4f, 4>,
};

@group(0) @binding(0) var<uniform> pc : DsPC;
@group(0) @binding(1) var<storage, read> pos : array<u32>;
@group(0) @binding(2) var<storage, read_write> idx : array<u32>;
@group(0) @binding(3) var<storage, read_write> keys : array<f32>;

fn f(w : u32) -> f32 { return bitcast<f32>(w); }

fn vertex(v : u32) -> vec3f {
    return vec3f(f(pos[v * 3u]), f(pos[v * 3u + 1u]), f(pos[v * 3u + 2u]));
}

/// Clip z / w of a world point — projectDepth3D.
fn depth3(p : vec3f) -> f32 {
    let cz = pc.vp[2].x * p.x + pc.vp[2].y * p.y + pc.vp[2].z * p.z +
             pc.vp[2].w;
    let cw = pc.vp[3].x * p.x + pc.vp[3].y * p.y + pc.vp[3].z * p.z +
             pc.vp[3].w;
    if (abs(cw) < 1e-30) { return 0.0; }
    return cz / cw;
}

@compute @workgroup_size(64)
fn keygen(@builtin(global_invocation_id) gid : vec3u) {
    let t = gid.x;
    if (t >= pc.nPad) { return; }
    if (t >= pc.nTris) {
        // Padding: sorts last (descending order), never drawn.
        keys[t] = -3.0e38;
        idx[t * 3u] = 0u; idx[t * 3u + 1u] = 1u; idx[t * 3u + 2u] = 2u;
        return;
    }
    // Seed the identity permutation, then key the triangle.
    idx[t * 3u] = t * 3u;
    idx[t * 3u + 1u] = t * 3u + 1u;
    idx[t * 3u + 2u] = t * 3u + 2u;
    let v0 = t * 3u;
    let d = (depth3(vertex(v0)) + depth3(vertex(v0 + 1u)) +
             depth3(vertex(v0 + 2u))) / 3.0;
    keys[t] = d;
}

@compute @workgroup_size(64)
fn bitonic(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.nPad) { return; }
    let partner = i ^ pc.j;
    if (partner <= i) { return; }
    // Descending: element i keeps the larger key when (i & k) == 0.
    let up = (i & pc.k) == 0u;
    let ki = keys[i];
    let kp = keys[partner];
    let swap = select(kp > ki, ki > kp, up);
    if (!swap) { return; }
    for (var c = 0u; c < 3u; c = c + 1u) {
        let a = idx[i * 3u + c];
        idx[i * 3u + c] = idx[partner * 3u + c];
        idx[partner * 3u + c] = a;
    }
    keys[i] = kp;
    keys[partner] = ki;
}
