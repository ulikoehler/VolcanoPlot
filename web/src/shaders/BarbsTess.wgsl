// web/src/shaders/BarbsTess.wgsl — wind-barb feather expansion.
//
// One invocation per barb. Mirrors BarbsPlot::buildBarbs exactly: the
// (x, y) sample is mapped to pixels, the shaft points FROM the wind
// direction, and the speed is decomposed into 50 kt flags, 10 kt full
// barbs and a single 5 kt half barb laid out from the tip down.
//
// Segment endpoints are written as vec2 pixels — the same buffer the
// line-segment pipeline consumes, so the rasterization is identical to
// the CPU path. The per-barb output offsets come from the CPU prefix
// pass (the count is a pure function of the speed), so there are no
// atomics and nothing is read back.

struct U {
    viewMinSpan : vec4f,   // xy = display min, zw = display span
    rect        : vec4f,   // pixel rect (x, y, w, h)
    color       : vec4f,
    scaleX      : vec4f,   // (code, p1, p2, pad)
    scaleY      : vec4f,
    proj        : vec4f,
    extra       : vec4f,
    extra2      : vec4f,
    n           : u32,
    flags       : u32,     // bit0 = config.flip
    totalVerts  : u32,
    length      : f32,
};

@group(0) @binding(0) var<uniform> u : U;
@group(0) @binding(1) var<storage, read> xb : array<f32>;
@group(0) @binding(2) var<storage, read> yb : array<f32>;
@group(0) @binding(3) var<storage, read> ub : array<f32>;
@group(0) @binding(4) var<storage, read> vb : array<f32>;
@group(0) @binding(5) var<storage, read> offs : array<u32>;
@group(0) @binding(6) var<storage, read_write> outp : array<f32>;

// scaleFwd: codes match plot::ScaleKind (see transform.wgsl).
fn scaleFwd(v : f32, s : vec4f) -> f32 {
    let c = i32(s.x + 0.5);
    if (c == 1) { return log(max(v, 1e-30)) / log(10.0); }      // log
    if (c == 2) {                                               // symlog
        let lt = s.y; let ls = s.z; let b = max(s.w, 1.000001);
        let adj = ls / (1.0 - pow(b, -1.0));
        let a = abs(v);
        if (a <= lt) { return adj * v; }
        return sign(v) * lt * (adj + log(a / lt) / log(b));
    }
    if (c == 3) {                                               // logit
        let q = clamp(v, 1e-7, 1.0 - 1e-7);
        return log(q / (1.0 - q));
    }
    if (c == 4) {                                               // asinh
        let a = max(s.y, 1e-30);
        return a * asinh(v / a);
    }
    if (c == 5) {                                               // mercator
        let phi = clamp(v, -1.48442223, 1.48442223);
        return log(tan(0.7853981633974483 + phi * 0.5));
    }
    return v;                                                   // linear
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= u.n) { return; }
    let flip = (u.flags & 1u) != 0u;
    let uv = vec2f(ub[i], vb[i]);
    let speed = sqrt(uv.x * uv.x + uv.y * uv.y);
    if (speed < 0.01) { return; }               // calm — no barb

    // data → pixels (mirrors Axes::dataToFraction + the pixel mapping).
    let v = vec2f(scaleFwd(xb[i], u.scaleX), scaleFwd(yb[i], u.scaleY));
    let sp = u.viewMinSpan.zw;
    var f = vec2f(0.5, 0.5);
    if (sp.x != 0.0) { f.x = (v.x - u.viewMinSpan.x) / sp.x; }
    if (sp.y != 0.0) { f.y = (v.y - u.viewMinSpan.y) / sp.y; }
    let px = u.rect.x + f.x * u.rect.z;
    let py = u.rect.y + (1.0 - f.y) * u.rect.w;

    // Shaft direction: the barb points FROM where the wind comes. Screen
    // +y is down while v is northward, hence the y flip.
    var dir = vec2f(-uv.x, uv.y);
    if (flip) { dir = vec2f(uv.x, -uv.y); }
    let dirLen = sqrt(dir.x * dir.x + dir.y * dir.y);
    if (dirLen < 1e-10) { return; }
    let ux = dir.x / dirLen;
    let uy = dir.y / dirLen;

    // Shaft length: pixels per display unit, clamped like the CPU.
    let pxPerDataX = select(0.0, u.rect.z / sp.x, sp.x != 0.0);
    let pxPerDataY = select(0.0, u.rect.w / sp.y, sp.y != 0.0);
    let shaftPx = clamp(u.length * min(pxPerDataX, pxPerDataY) / 7.0,
                        10.0, 40.0);
    let halfBarbLen = shaftPx * 0.18;
    let fullBarbLen = shaftPx * 0.28;
    let flagLen = shaftPx * 0.4;
    let barbSpacing = shaftPx * 0.15;
    let perp = vec2f(-uy, ux);

    // Decompose the speed into flags (50), full (10) and half (5) barbs.
    let speedInt = i32(round(speed / 5.0)) * 5;
    let nFlags = speedInt / 50;
    let remainder = speedInt % 50;
    let nFull = remainder / 10;
    let nHalf = (remainder % 10) / 5;

    // offs[] is a *segment* prefix (the CPU count pass counts segments);
    // outp is indexed in vertices — two per segment.
    let base = offs[i] * 2u;
    var k : u32 = 0u;
    var pos = 1.0;                              // fraction along the shaft
    let step = barbSpacing / shaftPx;

    // Shaft.
    outp[(base + k) * 2u] = px;
    outp[(base + k) * 2u + 1u] = py;
    k += 1u;
    outp[(base + k) * 2u] = px + ux * shaftPx;
    outp[(base + k) * 2u + 1u] = py + uy * shaftPx;
    k += 1u;

    // Flags: shaft → flag tip → shaft at the next position (pennant).
    for (var fi = 0; fi < nFlags; fi += 1) {
        let bx = px + ux * shaftPx * pos;
        let by = py + uy * shaftPx * pos;
        let ex = bx + perp.x * flagLen;
        let ey = by + perp.y * flagLen;
        outp[(base + k) * 2u] = bx;
        outp[(base + k) * 2u + 1u] = by;
        k += 1u;
        outp[(base + k) * 2u] = ex;
        outp[(base + k) * 2u + 1u] = ey;
        k += 1u;
        let nextPos = pos - step;
        let nx = px + ux * shaftPx * nextPos;
        let ny = py + uy * shaftPx * nextPos;
        outp[(base + k) * 2u] = ex;
        outp[(base + k) * 2u + 1u] = ey;
        k += 1u;
        outp[(base + k) * 2u] = nx;
        outp[(base + k) * 2u + 1u] = ny;
        k += 1u;
        pos = nextPos;
    }

    // Full barbs (10 kt), then at most one half barb (5 kt).
    for (var bi = 0; bi < nFull; bi += 1) {
        let bx = px + ux * shaftPx * pos;
        let by = py + uy * shaftPx * pos;
        outp[(base + k) * 2u] = bx;
        outp[(base + k) * 2u + 1u] = by;
        k += 1u;
        outp[(base + k) * 2u] = bx + perp.x * fullBarbLen;
        outp[(base + k) * 2u + 1u] = by + perp.y * fullBarbLen;
        k += 1u;
        pos -= step;
    }
    if (nHalf > 0) {
        let bx = px + ux * shaftPx * pos;
        let by = py + uy * shaftPx * pos;
        outp[(base + k) * 2u] = bx;
        outp[(base + k) * 2u + 1u] = by;
        k += 1u;
        outp[(base + k) * 2u] = bx + perp.x * halfBarbLen;
        outp[(base + k) * 2u + 1u] = by + perp.y * halfBarbLen;
        k += 1u;
    }
}
