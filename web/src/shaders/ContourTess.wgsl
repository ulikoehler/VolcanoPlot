// ContourTess.wgsl — GPU contour extraction + stroking.
//
// One invocation per (grid cell, level). Marching squares finds the
// segment where the level crosses the cell; the segment is expanded
// into a quad (6 vertices) and appended to a triangle soup through an
// atomic counter, which doubles as the indirect draw argument buffer.
// Nothing is read back — the C++ side never sees the geometry.
//
// Everything runs in grid-index space: the C++ side folds the grid
// origin and cell pitch into the affine, so the shader only needs
// `px = bx + i * ax`, `py = by + j * ay`.
//
// Tables mirror src/plot/plots/ContourPlot.cpp exactly
// (kMarchTable / kMarchSaddleAbove / interpEdge edge numbering).
//
// Vertex record = 6 f32: vec2 pos_px + vec4 rgba (DrawTrisGpu layout).
//
// Bindings:
//   0 uniform PContourTess
//   1 storage readonly grid   : array<f32>    (w*h, row-major)
//   2 storage readonly levels : array<f32>
//   3 storage readonly colors : array<vec4f>
//   4 storage read_write soup : array<f32>
//   5 storage read_write args : array<atomic<u32>>

struct ContourPC {
    gridW : u32, gridH : u32, nLevels : u32, pad : u32,
    bx : f32, ax : f32, by : f32, ay : f32,
    hwidth : f32, pad0 : f32, pad1 : f32, pad2 : f32,
    maxVerts : u32, pad3 : u32, pad4 : u32, dashMul : u32,
};

@group(0) @binding(0) var<uniform> pc : ContourPC;
@group(0) @binding(1) var<storage, read> grid : array<f32>;
@group(0) @binding(2) var<storage, read> levels : array<f32>;
@group(0) @binding(3) var<storage, read> colors : array<vec4f>;
@group(0) @binding(4) var<storage, read_write> soup : array<f32>;
@group(0) @binding(5) var<storage, read_write> args : array<atomic<u32>>;
/// (on, off) dash pattern per level — `on == 0` draws solid.
@group(0) @binding(6) var<storage, read> dashes : array<vec2f>;

// Edge pairs per marching-squares case; e0 < 0 means "no segment".
fn marchTable(code : u32) -> vec2i {
    switch (code) {
        case 1u:  { return vec2i(3, 0); }
        case 2u:  { return vec2i(0, 1); }
        case 3u:  { return vec2i(3, 1); }
        case 4u:  { return vec2i(1, 2); }
        case 5u:  { return vec2i(3, 2); }
        case 6u:  { return vec2i(0, 2); }
        case 7u:  { return vec2i(3, 2); }
        case 8u:  { return vec2i(2, 3); }
        case 9u:  { return vec2i(0, 2); }
        case 10u: { return vec2i(0, 3); }
        case 11u: { return vec2i(1, 2); }
        case 12u: { return vec2i(1, 3); }
        case 13u: { return vec2i(0, 1); }
        case 14u: { return vec2i(0, 3); }
        default:  { return vec2i(-1, -1); }
    }
}

fn marchSaddleAbove(code : u32) -> vec2i {
    switch (code) {
        case 1u:  { return vec2i(3, 0); }
        case 2u:  { return vec2i(0, 1); }
        case 3u:  { return vec2i(3, 1); }
        case 4u:  { return vec2i(1, 2); }
        case 5u:  { return vec2i(0, 1); }
        case 6u:  { return vec2i(0, 2); }
        case 7u:  { return vec2i(3, 2); }
        case 8u:  { return vec2i(2, 3); }
        case 9u:  { return vec2i(0, 2); }
        case 10u: { return vec2i(1, 2); }
        case 11u: { return vec2i(1, 2); }
        case 12u: { return vec2i(1, 3); }
        case 13u: { return vec2i(0, 1); }
        case 14u: { return vec2i(0, 3); }
        default:  { return vec2i(-1, -1); }
    }
}

/// Index-space crossing point of `level` on one cell edge, mapped to
/// pixels. edge 0=bottom (BL→BR), 1=right (BR→TR), 2=top (TR→TL),
/// 3=left (TL→BL).
fn interpEdge(edge : u32, level : f32, i : f32, j : f32,
              vBL : f32, vBR : f32, vTR : f32, vTL : f32) -> vec2f {
    var u = 0.0;   // position along the edge in index space
    var base = vec2f(i, j);
    var along = vec2f(1.0, 0.0);
    if (edge == 0u) {
        u = (level - vBL) / (vBR - vBL);
        base = vec2f(i, j);
        along = vec2f(1.0, 0.0);
    } else if (edge == 1u) {
        u = (level - vBR) / (vTR - vBR);
        base = vec2f(i + 1.0, j);
        along = vec2f(0.0, 1.0);
    } else if (edge == 2u) {
        u = (level - vTR) / (vTL - vTR);
        base = vec2f(i + 1.0, j + 1.0);
        along = vec2f(-1.0, 0.0);
    } else {
        u = (level - vTL) / (vBL - vTL);
        base = vec2f(i, j + 1.0);
        along = vec2f(0.0, -1.0);
    }
    let idx = base + along * u;
    return vec2f(pc.bx + idx.x * pc.ax, pc.by + idx.y * pc.ay);
}

/// Append one quad (6 vertices) for the run a→b. Returns false when the
/// soup budget is exhausted.
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

fn emitVert(idx : u32, p : vec2f, c : vec4f) {
    let b = idx * 6u;
    soup[b]      = p.x; soup[b + 1u] = p.y;
    soup[b + 2u] = c.r; soup[b + 3u] = c.g;
    soup[b + 4u] = c.b; soup[b + 5u] = c.a;
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let cols = pc.gridW - 1u;
    let rows = pc.gridH - 1u;
    let e = gid.x;
    if (e >= cols * rows * pc.nLevels) { return; }

    let cell = e / pc.nLevels;
    let li = e % pc.nLevels;
    let i = cell % cols;
    let j = cell / cols;

    let vBL = grid[j * pc.gridW + i];
    let vBR = grid[j * pc.gridW + i + 1u];
    let vTR = grid[(j + 1u) * pc.gridW + i + 1u];
    let vTL = grid[(j + 1u) * pc.gridW + i];
    if (vBL != vBL || vBR != vBR || vTR != vTR || vTL != vTL) { return; }

    let level = levels[li];
    var code = 0u;
    if (vBL >= level) { code = code | 1u; }
    if (vBR >= level) { code = code | 2u; }
    if (vTR >= level) { code = code | 4u; }
    if (vTL >= level) { code = code | 8u; }
    if (code == 0u || code == 15u) { return; }

    var pair = marchTable(code);
    if (code == 5u || code == 10u) {
        let center = (vBL + vBR + vTR + vTL) * 0.25;
        if (center >= level) { pair = marchSaddleAbove(code); }
    }
    if (pair.x < 0) { return; }

    let fi = f32(i);
    let fj = f32(j);
    let a = interpEdge(u32(pair.x), level, fi, fj, vBL, vBR, vTR, vTL);
    let b = interpEdge(u32(pair.y), level, fi, fj, vBL, vBR, vTR, vTL);

    let c = colors[li];
    let dash = dashes[li];
    if (dash.x <= 0.0) {
        let solidOk = emitQuad(a, b, c);
        if (!solidOk) { return; }
        return;
    }
    // Dashed level (mpl dashes negative levels). The CPU stroker dashes
    // each marching-squares segment independently — the pattern restarts
    // at every cell boundary — so walk this segment the same way.
    var d = b - a;
    let len = length(d);
    if (len < 1e-6) { return; }
    d = d / len;
    let period = dash.x + dash.y;
    var t = 0.0;
    var k = 0u;
    loop {
        if (t >= len || k >= pc.dashMul) { break; }
        let tEnd = min(t + dash.x, len);
        let ok = emitQuad(a + d * t, a + d * tEnd, c);
        if (!ok) { break; }
        t = t + period;
        k = k + 1u;
    }
}
