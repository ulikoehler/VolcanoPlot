// web/src/shaders/ReduceMinMax.wgsl — 2D min/max reduce (ReduceRenderer
// port). Single workgroup of 256 threads, grid-stride over a vec2f
// point buffer; order-preserving float→u32 key map lets us use
// atomicMin/atomicMax on u32 (WGSL has no float atomics).
//
//   0 uniform { count : u32, pad[3] }
//   1 storage pts : array<vec2f>  (read)
//   2 storage out : array<atomic<u32>, 4>  — [minX, maxX, minY, maxY]
//     as u32 keys; JS pre-initialises to {0xffffffff, 0, 0xffffffff, 0}
//     and decodes keys back to f32 before _vp_mailbox delivery.

struct RUBO { count : u32, pad0 : u32, pad1 : u32, pad2 : u32 };
@group(0) @binding(0) var<uniform> U : RUBO;
@group(0) @binding(1) var<storage, read> pts : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> out_ : array<atomic<u32>, 4>;

// Monotonic f32→u32: positives get the sign bit set, negatives get all
// bits inverted — unsigned order then matches float order.
fn enc(f : f32) -> u32 {
    let u = bitcast<u32>(f);
    return select(u | 0x80000000u, ~u, (u & 0x80000000u) != 0u);
}

var<workgroup> wg : array<atomic<u32>, 4>;

@compute @workgroup_size(256)
fn cs(@builtin(local_invocation_id) lid : vec3u) {
    if (lid.x == 0u) {
        atomicStore(&wg[0], 0xffffffffu);
        atomicStore(&wg[1], 0u);
        atomicStore(&wg[2], 0xffffffffu);
        atomicStore(&wg[3], 0u);
    }
    workgroupBarrier();
    var i = lid.x;
    while (i < U.count) {
        let pt = pts[i];
        atomicMin(&wg[0], enc(pt.x));
        atomicMax(&wg[1], enc(pt.x));
        atomicMin(&wg[2], enc(pt.y));
        atomicMax(&wg[3], enc(pt.y));
        i += 256u;
    }
    workgroupBarrier();
    if (lid.x == 0u) {
        atomicMin(&out_[0], atomicLoad(&wg[0]));
        atomicMax(&out_[1], atomicLoad(&wg[1]));
        atomicMin(&out_[2], atomicLoad(&wg[2]));
        atomicMax(&out_[3], atomicLoad(&wg[3]));
    }
}
