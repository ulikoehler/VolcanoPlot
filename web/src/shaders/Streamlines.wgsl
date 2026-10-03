// web/src/shaders/Streamlines.wgsl — RK4 streamline tracing (op 56).
// One invocation per candidate seed: traces the bilinear-interpolated
// U/V field forward and backward with the same RK4 step, coasting rules
// and grid-boundary break as plot::StreamPlot::integrateStreamline.
//
// The mpl seeding rule is order-dependent (a seed is rejected when it
// lands too close to an *already accepted* line), so the accept/reject
// loop stays on the CPU — it replays the same order over these traces.
//
//   0 uniform pc { w, h, maxPoints, flags, xMin, xSpan, yMin, ySpan,
//                  stepSize, nSeeds, pad, pad }
//   1 storage gridU   : array<f32>
//   2 storage gridV   : array<f32>
//   3 storage seeds   : array<f32>  (x, y pairs)
//   4 storage outPts  : array<f32>  (2 * maxPoints per seed —
//      backward slot first, then forward)
//   5 storage outCnt  : array<u32>  (2 per seed: nBack, nFwd)
//
// flags bit0 = brokenStreamlines.

struct SlPC {
    w : u32, h : u32, maxPoints : u32, flags : u32,
    xMin : f32, xSpan : f32, yMin : f32, ySpan : f32,
    stepSize : f32, nSeeds : u32, pad0 : u32, pad1 : u32,
};

@group(0) @binding(0) var<uniform> pc : SlPC;
@group(0) @binding(1) var<storage, read> gridU : array<f32>;
@group(0) @binding(2) var<storage, read> gridV : array<f32>;
@group(0) @binding(3) var<storage, read> seeds : array<f32>;
@group(0) @binding(4) var<storage, read_write> outPts : array<f32>;
@group(0) @binding(5) var<storage, read_write> outCnt : array<u32>;

fn sampleField(x : f32, y : f32) -> vec2f {
    if (x < pc.xMin || x > pc.xMin + pc.xSpan ||
        y < pc.yMin || y > pc.yMin + pc.ySpan) {
        return vec2f(0.0, 0.0);
    }
    let fx = (x - pc.xMin) / pc.xSpan * f32(pc.w - 1u);
    let fy = (y - pc.yMin) / pc.ySpan * f32(pc.h - 1u);
    let i0 = u32(fx);
    let j0 = u32(fy);
    let i1 = min(i0 + 1u, pc.w - 1u);
    let j1 = min(j0 + 1u, pc.h - 1u);
    let tx = fx - f32(i0);
    let ty = fy - f32(j0);
    let u00 = gridU[j0 * pc.w + i0]; let u10 = gridU[j0 * pc.w + i1];
    let u01 = gridU[j1 * pc.w + i0]; let u11 = gridU[j1 * pc.w + i1];
    let v00 = gridV[j0 * pc.w + i0]; let v10 = gridV[j0 * pc.w + i1];
    let v01 = gridV[j1 * pc.w + i0]; let v11 = gridV[j1 * pc.w + i1];
    let u = u00 * (1.0 - tx) * (1.0 - ty) + u10 * tx * (1.0 - ty) +
            u01 * (1.0 - tx) * ty + u11 * tx * ty;
    let v = v00 * (1.0 - tx) * (1.0 - ty) + v10 * tx * (1.0 - ty) +
            v01 * (1.0 - tx) * ty + v11 * tx * ty;
    return vec2f(u, v);
}

/// RK4 step — same expression order as plot::rk4Step.
fn rk4Step(x : f32, y : f32, hstep : f32) -> vec2f {
    let k1 = sampleField(x, y);
    let k2 = sampleField(x + 0.5 * hstep * k1.x, y + 0.5 * hstep * k1.y);
    let k3 = sampleField(x + 0.5 * hstep * k2.x, y + 0.5 * hstep * k2.y);
    let k4 = sampleField(x + hstep * k2.x, y + hstep * k2.y);
    return vec2f(x + hstep * (k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x) / 6.0,
                 y + hstep * (k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y) / 6.0);
}

fn trace(x0 : f32, y0 : f32, dir : i32, h : f32, slotBase : u32) -> u32 {
    let broken = (pc.flags & 1u) != 0u;
    var x = x0; var y = y0;
    var lastU = 0.0; var lastV = 0.0; var maxMag = 0.0;
    var n = 0u;
    for (var step = 0u; step < pc.maxPoints; step = step + 1u) {
        if (n < pc.maxPoints) {
            outPts[(slotBase + n) * 2u] = x;
            outPts[(slotBase + n) * 2u + 1u] = y;
        }
        n = n + 1u;
        let uv = sampleField(x, y);
        let mag = sqrt(uv.x * uv.x + uv.y * uv.y);
        if (!(mag == mag) || mag < 1e-10) {   // NaN or calm
            if (broken || (lastU == 0.0 && lastV == 0.0)) { break; }
            x = x + h * lastU;
            y = y + h * lastV;
            if (x < pc.xMin || x > pc.xMin + pc.xSpan ||
                y < pc.yMin || y > pc.yMin + pc.ySpan) { break; }
            continue;
        }
        maxMag = max(maxMag, mag);
        if (mag >= 0.5 * maxMag) { lastU = uv.x; lastV = uv.y; }
        var next = rk4Step(x, y, h);
        if (!(next.x == next.x) || !(next.y == next.y)) {
            if (broken || (lastU == 0.0 && lastV == 0.0)) { break; }
            next = vec2f(x + h * lastU, y + h * lastV);
        }
        if (next.x < pc.xMin || next.x > pc.xMin + pc.xSpan ||
            next.y < pc.yMin || next.y > pc.yMin + pc.ySpan) { break; }
        x = next.x; y = next.y;
    }
    return n;
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let s = gid.x;
    if (s >= pc.nSeeds) { return; }
    let x0 = seeds[s * 2u];
    let y0 = seeds[s * 2u + 1u];
    let dx = pc.xSpan / f32(pc.w - 1u);
    let dy = pc.ySpan / f32(pc.h - 1u);
    let cellSize = min(dx, dy);
    // Backward occupies the first half of the seed's slot, forward the
    // second (the host reverses the backward run, as the CPU does).
    let nBack = trace(x0, y0, -1, pc.stepSize * cellSize * -1.0,
                      s * 2u * pc.maxPoints);
    let nFwd = trace(x0, y0, 1, pc.stepSize * cellSize,
                     s * 2u * pc.maxPoints + pc.maxPoints);
    outCnt[s * 2u] = nBack;
    outCnt[s * 2u + 1u] = nFwd;
}
