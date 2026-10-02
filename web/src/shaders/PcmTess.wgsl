// web/src/shaders/PcmTess.wgsl — pcolormesh quad tessellation.
// Port of VulkanGpuServices.cpp kPcmTessGlsl: one thread per cell.
// Outputs pos(vec2)/col(vec4) vertex buffers read by DrawTrisData.

struct PC {
    nCols : u32,    // cells per row (flat) or corner count (gouraud)
    nRows : u32,
    gouraud : u32,
    flags : u32,    // bit0 = cmap.bad set, bit1 = skipNaN
}
@group(0) @binding(0) var<uniform> pc : PC;
@group(0) @binding(1) var<storage, read> xs : array<f32>;
@group(0) @binding(2) var<storage, read> ys : array<f32>;
@group(0) @binding(3) var<storage, read> ts : array<f32>;
@group(0) @binding(4) var<storage, read> lut : array<vec4f>;
@group(0) @binding(5) var<storage, read_write> pos : array<vec2f>;
@group(0) @binding(6) var<storage, read_write> col : array<vec4f>;

fn colorOf(t : f32) -> vec4f {
    if (t != t) { return lut[258]; }              // NaN → bad
    if (t < 0.0) { return lut[256]; }
    if (t > 1.0) { return lut[257]; }
    return lut[u32(t * 255.0 + 0.5)];
}

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) g : vec3u) {
    let cell = g.x;
    if (pc.gouraud == 0u) {
        if (cell >= pc.nCols * pc.nRows) { return; }
        let i = cell % pc.nCols;
        let j = cell / pc.nCols;
        let c = colorOf(ts[cell]);
        let bl = vec2f(xs[i],     ys[j]);
        let br = vec2f(xs[i + 1], ys[j]);
        let ur = vec2f(xs[i + 1], ys[j + 1]);
        let ul = vec2f(xs[i],     ys[j + 1]);
        let v = cell * 6u;
        pos[v]      = bl; pos[v + 1u] = br; pos[v + 2u] = ul;
        pos[v + 3u] = br; pos[v + 4u] = ur; pos[v + 5u] = ul;
        for (var k = 0u; k < 6u; k++) { col[v + k] = c; }
    } else {
        // Gouraud: (nCols-1)×(nRows-1) quads, 4 tris meeting at the
        // averaged center vertex (mpl _convert_mesh_to_triangles).
        let qw = pc.nCols - 1u;
        if (cell >= qw * (pc.nRows - 1u)) { return; }
        let i = cell % qw;
        let j = cell / qw;
        let ta = ts[j * pc.nCols + i];
        let tb = ts[j * pc.nCols + i + 1u];
        let tc = ts[(j + 1u) * pc.nCols + i + 1u];
        let td = ts[(j + 1u) * pc.nCols + i];
        var ca = colorOf(ta); var cb = colorOf(tb);
        var cc = colorOf(tc); var cd = colorOf(td);
        // mpl drops the quad if any corner is masked and no bad color.
        let anyNan = (ta != ta) || (tb != tb) || (tc != tc) || (td != td);
        if ((pc.flags & 3u) == 2u && anyNan) {
            ca = vec4f(0.0); cb = vec4f(0.0);
            cc = vec4f(0.0); cd = vec4f(0.0);
        }
        let cCtr = (ca + cb + cc + cd) * 0.25;
        let pa = vec2f(xs[i],     ys[j]);
        let pb = vec2f(xs[i + 1], ys[j]);
        let pq = vec2f(xs[i + 1], ys[j + 1]);
        let pd = vec2f(xs[i],     ys[j + 1]);
        let pCtr = (pa + pb + pq + pd) * 0.25;
        let v = cell * 12u;
        let pts = array<vec2f, 12>(pa, pb, pCtr, pb, pq, pCtr,
                                   pq, pd, pCtr, pd, pa, pCtr);
        let cls = array<vec4f, 12>(ca, cb, cCtr, cb, cc, cCtr,
                                   cc, cd, cCtr, cd, ca, cCtr);
        for (var k = 0u; k < 12u; k++) {
            pos[v + k] = pts[k];
            col[v + k] = cls[k];
        }
    }
}
