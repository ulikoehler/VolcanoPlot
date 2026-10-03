// TessLines.wgsl — WGSL port of kTessGlsl in GpuLineRenderer.cpp.
// One invocation per segment (quad, 6 verts) + one per point
// (join/cap, 24 verts). Output record = 6 f32: vec2f pos_px + vec4f
// color; consumed by DrawTrisGpu in the same frame.
//
// Bindings:
//   0 uniform PTessLines (fields packed to 16 B lanes)
//   1 storage readonly pts : array<vec2f>
//   2 storage rw       out : array<f32>

struct TessPC {
    // scalar fields packed individually (uniform buffer layout)
    n : u32, nSeg : u32, hwidth : f32, join : u32,
    cap : u32, miterLimit : f32, inBase : u32, outBase : u32,
    color : vec4f,
    // Dashes (dashMul == 1 → solid stroke, unchanged layout).
    dashMul : u32, dashCount : u32, lenBase : u32, dashBase : u32,
    dashOffset : f32, pad0 : f32, pad1 : f32, pad2 : f32,
};

@group(0) @binding(0) var<uniform> pc : TessPC;
@group(0) @binding(1) var<storage, read> pts : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> vout : array<f32>;
/// Per-point cumulative arc length (only read when dashed).
@group(0) @binding(3) var<storage, read> cum : array<f32>;
/// Dash pattern (on, off, on, off, …) in pixels.
@group(0) @binding(4) var<storage, read> dashPat : array<f32>;

const SEGV : u32 = 6u;
const JOINV : u32 = 24u;

fn perp2(d : vec2f) -> vec2f { return vec2f(-d.y, d.x); }

fn finitePt(i : u32) -> bool {
    let p = pts[pc.inBase + i];
    return p.x == p.x && p.y == p.y &&
           abs(p.x) < 1e30 && abs(p.y) < 1e30;
}

fn emitVert(idx : u32, p : vec2f) {
    let b = (pc.outBase + idx) * 6u;
    vout[b]      = p.x;        vout[b + 1u] = p.y;
    vout[b + 2u] = pc.color.r; vout[b + 3u] = pc.color.g;
    vout[b + 4u] = pc.color.b; vout[b + 5u] = pc.color.a;
}
fn emitTri(idx : u32, a : vec2f, b : vec2f, c : vec2f) {
    emitVert(idx, a); emitVert(idx + 1u, b); emitVert(idx + 2u, c);
}
fn emitQuad(idx : u32, a : vec2f, b : vec2f, n : vec2f) {
    emitTri(idx, a - n, a + n, b + n);
    emitTri(idx + 3u, a - n, b + n, b - n);
}

/// Pattern period (sum of the dash lengths).
fn dashPeriod() -> f32 {
    var p = 0.0;
    for (var i = 0u; i < pc.dashCount; i = i + 1u) {
        p = p + dashPat[pc.dashBase + i];
    }
    return p;
}

/// Walk one segment in cumulative-arc-length space, emitting a quad per
/// visible dash. Mirrors plot::dashSplit: the pattern is continuous
/// along the polyline (the phase comes from the cumulative length), and
/// dash runs get butt/projecting/round ends like the CPU stroker.
fn emitDashedSeg(i : u32, a : vec2f, b : vec2f, len : f32,
                 d : vec2f, n : vec2f, slot : u32) {
    let period = dashPeriod();
    if (period <= 0.0) { return; }
    let s0 = cum[pc.lenBase + i];
    let s1 = cum[pc.lenBase + i + 1u];
    if (s1 <= s0) { return; }

    // Extend a dash by half the width at each end for the projecting
    // cap; round caps keep the same footprint (the arc is negligible at
    // dash scale), butt caps take the interval as-is.
    var grow = 0.0;
    if (pc.cap == 2u) { grow = pc.hwidth; }

    // First phase boundary at or before s0.
    var k = floor((s0 + pc.dashOffset) / period);
    var slotCur = slot;
    loop {
        let base = k * period - pc.dashOffset;
        if (base > s1) { break; }
        // Dash `on` runs occupy [base, base + on0] within the pattern.
        var acc = base;
        for (var j = 0u; j < pc.dashCount; j = j + 1u) {
            let dl = dashPat[pc.dashBase + j];
            if (acc > s1) { break; }
            if ((j & 1u) == 0u) {
                let lo = max(acc - grow, s0);
                let hi = min(acc + dl + grow, s1);
                if (hi > lo) {
                    if (slotCur + 6u > slot + 6u * pc.dashMul) { return; }
                    let t0 = (lo - s0) / len;
                    let t1 = (hi - s0) / len;
                    emitQuad(slotCur, a + d * (t0 * len),
                             a + d * (t1 * len), n);
                    slotCur = slotCur + 6u;
                }
            }
            acc = acc + dl;
        }
        k = k + 1.0;
    }
}
fn zeroSlot(idx : u32, count : u32) {
    for (var k = 0u; k < count; k = k + 1u) {
        let b = (pc.outBase + idx + k) * 6u;
        for (var f = 0u; f < 6u; f = f + 1u) { vout[b + f] = 0.0; }
    }
}

fn arcFan(idx : u32, apex : vec2f, c : vec2f, h : f32,
          aFrom : vec2f, aTo : vec2f, through : vec2f, steps : u32) {
    let TAU = 6.28318530718;
    let a0 = atan2(aFrom.y - c.y, aFrom.x - c.x);
    let a1 = atan2(aTo.y - c.y, aTo.x - c.x);
    let am = atan2(through.y - c.y, through.x - c.x);
    var ccw = glslMod(a1 - a0, TAU);
    if (ccw < 1e-6) { ccw = TAU; }
    let mOnCcw = glslMod(am - a0, TAU);
    let dir = select(-1.0, 1.0, mOnCcw <= ccw);
    let span = select(-(TAU - ccw), ccw, dir > 0.0);
    var prev = aFrom;
    var vi = idx;
    for (var k = 1u; k <= steps; k = k + 1u) {
        let a = a0 + span * f32(k) / f32(steps);
        let cur = c + vec2f(cos(a), sin(a)) * h;
        emitTri(vi, apex, prev, cur);
        prev = cur;
        vi = vi + 3u;
    }
}

@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let e = gid.x;
    if (e >= pc.nSeg + pc.n) { return; }
    if (pc.n < 2u) { return; }

    if (e < pc.nSeg) {
        // segment quad pts[i] -> pts[i+1]
        let i = e;
        let slot = i * SEGV * pc.dashMul;
        let a = pts[pc.inBase + i];
        let b = pts[pc.inBase + i + 1u];
        let d = b - a;
        let len = length(d);
        if (len < 1e-6 || !finitePt(i) || !finitePt(i + 1u)) {
            zeroSlot(slot, SEGV * pc.dashMul);
            return;
        }
        let n = perp2(d / len) * pc.hwidth;
        if (pc.dashMul > 1u && pc.dashCount > 0u) {
            // Dashed: joins are not stroked (mpl does not join dashes),
            // so leave the join slots alone and dash this segment.
            zeroSlot(slot, SEGV * pc.dashMul);
            emitDashedSeg(i, a, b, len, d / len, n, slot);
            return;
        }
        emitTri(slot,      a - n, a + n, b + n);
        emitTri(slot + 3u, a - n, b + n, b - n);
        return;
    }

    // per-point join or cap slot
    let i = e - pc.nSeg;
    if (pc.dashMul > 1u && pc.dashCount > 0u) { return; }  // no joins
    let slot = pc.nSeg * SEGV * pc.dashMul + i * JOINV;
    let prevOk = i > 0u && finitePt(i - 1u) && finitePt(i);
    let nextOk = i + 1u < pc.n && finitePt(i + 1u) && finitePt(i);
    let p = pts[pc.inBase + i];

    if (prevOk && nextOk) {
        let pa = pts[pc.inBase + i - 1u];
        let pb = pts[pc.inBase + i + 1u];
        var d0 = p - pa; var d1 = pb - p;
        let l0 = length(d0); let l1 = length(d1);
        if (l0 < 1e-6 || l1 < 1e-6) { zeroSlot(slot, JOINV); return; }
        d0 = d0 / l0; d1 = d1 / l1;
        let n0 = perp2(d0); let n1 = perp2(d1);
        let crs = d0.x * d1.y - d0.y * d1.x;
        if (abs(crs) < 1e-6) { zeroSlot(slot, JOINV); return; }
        let s = select(-1.0, 1.0, crs > 0.0);
        let oA = p + n0 * (s * pc.hwidth);
        let oB = p + n1 * (s * pc.hwidth);
        if (pc.join == 0u) {
            let m = n0 + n1;
            let ml = length(m);
            let mdir = select(n0, m / ml, ml > 1e-6);
            let dt = max(dot(mdir, n0), 1e-6);
            if (dt >= 1.0 / pc.miterLimit) {
                emitTri(slot, oA, p + mdir * (s * pc.hwidth / dt), oB);
                zeroSlot(slot + 3u, JOINV - 3u);
            } else {
                zeroSlot(slot, JOINV);
            }
        } else if (pc.join == 1u) {
            arcFan(slot, p, p, pc.hwidth, oA, oB, p + (n0 + n1) * s, 8u);
        } else {
            zeroSlot(slot, JOINV);   // bevel: chord already covered
        }
        return;
    }

    if (!finitePt(i)) { zeroSlot(slot, JOINV); return; }

    let isStart = !prevOk && nextOk;
    let isEnd = prevOk && !nextOk;
    if (!isStart && !isEnd) { zeroSlot(slot, JOINV); return; }
    if (pc.cap == 0u) { zeroSlot(slot, JOINV); return; }

    let other = select(pts[pc.inBase + i - 1u],
                       pts[pc.inBase + i + 1u], isStart);
    var d = other - p;
    let len = length(d);
    if (len < 1e-6) { zeroSlot(slot, JOINV); return; }
    d = d / len;
    if (isStart) { d = -d; }
    let n = perp2(d) * pc.hwidth;

    if (pc.cap == 2u) {
        let o = p + d * pc.hwidth;
        emitTri(slot,      p - n, p + n, o + n);
        emitTri(slot + 3u, p - n, o + n, o - n);
        zeroSlot(slot + 6u, JOINV - 6u);
    } else {
        arcFan(slot, p, p, pc.hwidth, p - n, p + n, p + d, 8u);
    }
}
