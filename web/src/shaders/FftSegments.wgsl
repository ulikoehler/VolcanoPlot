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
// `n` is capped at 2048 by the caller (2 × n floats of workgroup
// storage = 16 KB, exactly the spec-guaranteed
// maxComputeWorkgroupStorageSize). Larger transforms stay on the
// CPU FFT.
//
// Bindings:
//   0 uniform PFftSegments
//   1 storage readonly sig : array<f32>
//   2 storage readonly win : array<f32>
//   3 storage rw       out : array<f32>

struct FftPC {
    n : u32, step : u32, numSegs : u32, sigLen : u32,
};

@group(0) @binding(0) var<uniform> pc : FftPC;
@group(0) @binding(1) var<storage, read> sig : array<f32>;
@group(0) @binding(2) var<storage, read> win : array<f32>;
@group(0) @binding(3) var<storage, read_write> out : array<f32>;

const MAXN : u32 = 2048u;
const TPB  : u32 = 256u;

var<workgroup> re : array<f32, MAXN>;
var<workgroup> im : array<f32, MAXN>;

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

    // Load, window, and bit-reverse into place.
    let base = seg * pc.step;
    for (var i = lid; i < n; i = i + TPB) {
        let gi = base + i;
        var sample = 0.0;
        if (gi < pc.sigLen) { sample = sig[gi]; }
        let w = select(0.0, win[i], gi < pc.sigLen);
        let j = bitrev(i, bits);
        re[j] = sample * w;
        im[j] = 0.0;
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
