// web/src/shaders/TransformPoints.wgsl — device-side data→pixel mapping.
//
// One invocation per point. Mirrors plot::Axes::dataToFraction followed
// by the pixel mapping in LinePlot::pixelPoints:
//
//   f  = (scaleFwd(v) - viewMin) / viewSpan      (display space, Y-up)
//   px = { rect.x + f.x·rect.w, rect.y + (1-f.y)·rect.h }
//
// Out-of-domain points (log/logit) and non-finite inputs become NaN, so
// the downstream stroker emits degenerate segments exactly like the CPU
// maskPointsForScales path.

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
};

@group(0) @binding(0) var<uniform> u : U;
@group(0) @binding(1) var<storage, read> pts : array<f32>;
@group(0) @binding(2) var<storage, read_write> outp : array<f32>;

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

// AxisScale::inDomain — which values the scale drops.
fn inDomain(v : f32, code : i32) -> bool {
    if (code == 1 || code == 7) { return v > 0.0; }             // log, fnlog
    if (code == 3) { return v > 0.0 && v < 1.0; }               // logit
    return true;
}

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= u.n) { return; }
    let d = vec2f(pts[i * 2u], pts[i * 2u + 1u]);
    let cx = i32(u.scaleX.x + 0.5);
    let cy = i32(u.scaleY.x + 0.5);
    // Masked points split the polyline (NaN propagates through the
    // stroker) — same contract as the CPU mask pass.
    if (!(inDomain(d.x, cx) && inDomain(d.y, cy)) ||
        !(d.x == d.x) || !(d.y == d.y) ||
        abs(d.x) > 3.4e38 || abs(d.y) > 3.4e38) {
        // quiet NaN — the +0u keeps the bitcast out of const-eval,
        // which rejects materializing NaN.
        let qnan = bitcast<f32>(0x7fc00000u + u.n * 0u);
        outp[i * 2u] = qnan;
        outp[i * 2u + 1u] = qnan;
        return;
    }
    let v = vec2f(scaleFwd(d.x, u.scaleX), scaleFwd(d.y, u.scaleY));
    let sp = u.viewMinSpan.zw;
    var f = vec2f(0.5, 0.5);
    if (sp.x != 0.0) { f.x = (v.x - u.viewMinSpan.x) / sp.x; }
    if (sp.y != 0.0) { f.y = (v.y - u.viewMinSpan.y) / sp.y; }
    outp[i * 2u] = u.rect.x + f.x * u.rect.z;
    outp[i * 2u + 1u] = u.rect.y + (1.0 - f.y) * u.rect.w;
}
