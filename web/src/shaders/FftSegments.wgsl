// FftSegments.wgsl — batched real-input FFT (spectrum family).
//
// One workgroup per segment. Each workgroup loads its windowed samples
// into workgroup storage, bit-reverses, then runs the iterative
// radix-2 Cooley-Tukey butterflies. Output is n complex values
// (interleaved re, im) per segment, read back through the bulk mailbox.
//
// The per-plot spectral math (magnitude, dB, phase unwrap, cross
// spectra, Welch averaging) stays on the CPU — every member of the
// spectrum family shares this one primitive.
//
// `n` is capped at 1024 for the workgroup path (`cs`): storage is
// 2×n floats for the transform plus 2×256 for the detrend
// reductions = 10 KB at the cap, under the 16 KB spec-guaranteed
// maxComputeWorkgroupStorageSize. Larger transforms use the
// global-memory path (`segprep` + one `bfly` dispatch per stage);
// each butterfly pair is disjoint, so in-place passes are safe.
//
// Bindings:
//   0 uniform PFftSegments
//   1 storage readonly sig : array<f32>
//   2 storage readonly win : array<f32>
//   3 storage rw       out : array<f32>

// `detrend` (map kernel, run per segment before the window): 0 none,
// 1 mean (mpl 'default'/'mean'/'constant'), 2 linear (least-squares
// line). Fusing it here keeps the segment pass on the device — the
// host would otherwise walk every segment to subtract the trend.
struct FftPC {
    n : u32, step : u32, numSegs : u32, sigLen : u32,
    detrend : u32, stage : u32, pad2 : u32, pad3 : u32,
};

@group(0) @binding(0) var<uniform> pc : FftPC;
@group(0) @binding(1) var<storage, read> sig : array<f32>;
@group(0) @binding(2) var<storage, read> win : array<f32>;
@group(0) @binding(3) var<storage, read_write> out : array<f32>;

const MAXN : u32 = 1024u;
const TPB  : u32 = 256u;

var<workgroup> re : array<f32, MAXN>;
var<workgroup> im : array<f32, MAXN>;
// Detrend reductions: Σx and Σ(i·x) over the segment.
var<workgroup> red : array<f32, TPB>;
var<workgroup> redI : array<f32, TPB>;

/// Reverse the low `bits` bits of `v`.
fn bitrev(v : u32, bits : u32) -> u32 {
    var x = v;
    var r = 0u;
    for (var i = 0u; i < bits; i = i + 1u) {
        r = (r << 1u) | (x & 1u);
        x = x >> 1u;
    }
    return r;
}

@compute @workgroup_size(256)
fn cs(@builtin(workgroup_id) wid : vec3u,
      @builtin(local_invocation_index) lid : u32) {
    let n = pc.n;
    if (n < 2u || n > MAXN) { return; }
    let seg = wid.x;
    if (seg >= pc.numSegs) { return; }

    // log2(n)
    var bits = 0u;
    var t = n;
    loop {
        if (t <= 1u) { break; }
        t = t >> 1u;
        bits = bits + 1u;
    }

    // Load and bit-reverse the raw samples; accumulate the detrend sums
    // in the same pass.
    let base = seg * pc.step;
    var sum = 0.0;
    var sumI = 0.0;
    for (var i = lid; i < n; i = i + TPB) {
        let gi = base + i;
        var sample = 0.0;
        if (gi < pc.sigLen) { sample = sig[gi]; }
        sum = sum + sample;
        sumI = sumI + f32(i) * sample;
        re[bitrev(i, bits)] = sample;
        im[bitrev(i, bits)] = 0.0;
    }
    red[lid] = sum;
    redI[lid] = sumI;
    workgroupBarrier();
    var s = TPB >> 1u;
    loop {
        if (s == 0u) { break; }
        if (lid < s) { red[lid] = red[lid] + red[lid + s];
                       redI[lid] = redI[lid] + redI[lid + s]; }
        workgroupBarrier();
        s = s >> 1u;
    }

    // Trend: mean, or the least-squares line over x = 0..n-1 (mpl
    // detrend_mean / detrend_linear; the CPU uses f64 here, so very
    // large segments may differ in the last few bits).
    let nf = f32(n);
    var slope = 0.0;
    var icept = 0.0;
    if (pc.detrend == 1u) {
        icept = red[0] / nf;
    } else if (pc.detrend == 2u) {
        let si = nf * (nf - 1.0) * 0.5;
        let sii = (nf - 1.0) * nf * (2.0 * nf - 1.0) / 6.0;
        let det = nf * sii - si * si;
        slope = select(0.0, (nf * redI[0] - si * red[0]) / det, det != 0.0);
        icept = (red[0] - slope * si) / nf;
    }

    // Window (and detrend) into place.
    for (var i = lid; i < n; i = i + TPB) {
        let gi = base + i;
        let w = select(0.0, win[i], gi < pc.sigLen);
        let j = bitrev(i, bits);
        re[j] = (re[j] - (slope * f32(i) + icept)) * w;
    }
    workgroupBarrier();

    // Iterative butterflies. `len` is uniform across the workgroup, so
    // the barriers sit outside any divergent loop.
    var len = 2u;
    loop {
        if (len > n) { break; }
        let half = len >> 1u;
        for (var i = lid; i < n; i = i + TPB) {
            let blk = (i / half) * len + (i % half);
            let j = blk + half;
            let k = i % half;
            let ang = -6.283185307179586 * f32(k) / f32(len);
            let wr = cos(ang);
            let wi = sin(ang);
            let ar = re[blk];
            let ai = im[blk];
            let br = re[j];
            let bi = im[j];
            let tr = br * wr - bi * wi;
            let ti = br * wi + bi * wr;
            re[blk] = ar + tr;
            im[blk] = ai + ti;
            re[j] = ar - tr;
            im[j] = ai - ti;
        }
        workgroupBarrier();
        len = len << 1u;
    }

    // Write the complex spectrum interleaved.
    for (var i = lid; i < n; i = i + TPB) {
        let o = (seg * n + i) * 2u;
        out[o] = re[i];
        out[o + 1u] = im[i];
    }
}

// ── global-memory path (n > MAXN) ───────────────────────────────────
// `segprep` runs one serial invocation per segment: the detrend sums
// and the bit-reversed, detrended, windowed write of the real samples
// into `out`. `bfly` then runs log2(n) in-place butterfly passes
// (pc.stage = log2(len) - 1); pairs inside a stage are disjoint.

@compute @workgroup_size(64)
fn segprep(@builtin(global_invocation_id) gid : vec3u) {
    let seg = gid.x;
    let n = pc.n;
    if (seg >= pc.numSegs || n < 2u) { return; }
    var bits = 0u;
    var t = n;
    loop {
        if (t <= 1u) { break; }
        t = t >> 1u;
        bits = bits + 1u;
    }
    let base = seg * pc.step;
    var sum = 0.0;
    var sumI = 0.0;
    for (var i = 0u; i < n; i = i + 1u) {
        let gi = base + i;
        var sample = 0.0;
        if (gi < pc.sigLen) { sample = sig[gi]; }
        sum = sum + sample;
        sumI = sumI + f32(i) * sample;
    }
    let nf = f32(n);
    var slope = 0.0;
    var icept = 0.0;
    if (pc.detrend == 1u) {
        icept = sum / nf;
    } else if (pc.detrend == 2u) {
        let si = nf * (nf - 1.0) * 0.5;
        let sii = (nf - 1.0) * nf * (2.0 * nf - 1.0) / 6.0;
        let det = nf * sii - si * si;
        slope = select(0.0, (nf * sumI - si * sum) / det, det != 0.0);
        icept = (sum - slope * si) / nf;
    }
    for (var i = 0u; i < n; i = i + 1u) {
        let gi = base + i;
        var sample = 0.0;
        if (gi < pc.sigLen) { sample = sig[gi]; }
        let w = select(0.0, win[i], gi < pc.sigLen);
        let j = bitrev(i, bits);
        let o = (seg * n + j) * 2u;
        out[o] = (sample - (slope * f32(i) + icept)) * w;
        out[o + 1u] = 0.0;
    }
}

@compute @workgroup_size(256)
fn bfly(@builtin(global_invocation_id) gid : vec3u) {
    let n = pc.n;
    let totalPairs = pc.numSegs * (n >> 1u);
    if (gid.x >= totalPairs) { return; }
    let half = 1u << pc.stage;
    let len = half << 1u;
    let seg = gid.x / (n >> 1u);
    let p = gid.x % (n >> 1u);
    let k = p % half;
    let i = seg * n + (p / half) * len + k;
    let j = i + half;
    let ang = -6.283185307179586 * f32(k) / f32(len);
    let wr = cos(ang);
    let wi = sin(ang);
    let ar = out[i * 2u];
    let ai = out[i * 2u + 1u];
    let br = out[j * 2u];
    let bi = out[j * 2u + 1u];
    let tr = br * wr - bi * wi;
    let ti = br * wi + bi * wr;
    out[i * 2u] = ar + tr;
    out[i * 2u + 1u] = ai + ti;
    out[j * 2u] = ar - tr;
    out[j * 2u + 1u] = ai - ti;
}
