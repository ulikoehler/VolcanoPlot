// web/src/shaders/SortFloats.wgsl — bitonic value sort (op 60). The
// host seeds the buffer padded to a power of two with +inf; the
// interpreter runs every (k, j) compare-exchange stage; the first
// nReal floats are then copied to the mailbox readback.
//
//   0 uniform pc { nPad, k, j, pad }
//   1 storage vals : array<f32>

struct SfPC {
    nPad : u32, k : u32, j : u32, pad : u32,
};

@group(0) @binding(0) var<uniform> pc : SfPC;
@group(0) @binding(1) var<storage, read_write> vals : array<f32>;

@compute @workgroup_size(64)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.nPad) { return; }
    let ixj = i ^ pc.j;
    if (ixj > i) {
        let a = vals[i];
        let b = vals[ixj];
        let asc = (i & pc.k) == 0u;
        if ((asc && a > b) || (!asc && a < b)) {
            vals[i] = b;
            vals[ixj] = a;
        }
    }
}
