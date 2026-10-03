// HexBins.wgsl — mpl hexbin (pointy-top) lattice accumulation.
//
// mpl (axes/_axes.py::hexbin) uses two interleaved lattices:
//   A = (nx+1)x(ny+1) at (i*sx, j*sy)
//   B = nx*ny          at ((i+.5)*sx, (j+.5)*sy)
// Each point goes to the nearer cell under the hex metric
// d = dx^2 + 3*dy^2 measured in index space.
//
// Bindings:
//   0 uniform PHexBins
//   1 storage readonly xy  : array<vec2f>
//   2 storage rw       cnt : array<atomic<u32>>   (A then B)

struct HexPC {
    n : u32, nx : u32, ny : u32, pad : u32,
    xMin : f32, yMin : f32, sx : f32, sy : f32,
};

@group(0) @binding(0) var<uniform> pc : HexPC;
@group(0) @binding(1) var<storage, read> xy : array<vec2f>;
@group(0) @binding(2) var<storage, read_write> cnt : array<atomic<u32>>;

@compute @workgroup_size(256)
fn cs(@builtin(global_invocation_id) gid : vec3u) {
    let k = gid.x;
    if (k >= pc.n) { return; }
    let p = xy[k];
    if (p.x != p.x || p.y != p.y) { return; }

    let ix = (p.x - pc.xMin) / pc.sx;
    let iy = (p.y - pc.yMin) / pc.sy;
    let ix1 = i32(round(ix));
    let iy1 = i32(round(iy));
    let ix2 = i32(floor(ix));
    let iy2 = i32(floor(iy));
    let dx1 = ix - f32(ix1);
    let dy1 = iy - f32(iy1);
    let dx2 = ix - f32(ix2) - 0.5;
    let dy2 = iy - f32(iy2) - 0.5;
    let d1 = dx1 * dx1 + 3.0 * dy1 * dy1;
    let d2 = dx2 * dx2 + 3.0 * dy2 * dy2;

    if (d1 < d2) {
        if (ix1 >= 0 && ix1 <= i32(pc.nx) && iy1 >= 0 && iy1 <= i32(pc.ny)) {
            let slot = u32(ix1) * (pc.ny + 1u) + u32(iy1);
            atomicAdd(&cnt[slot], 1u);
        }
    } else {
        if (ix2 >= 0 && ix2 < i32(pc.nx) && iy2 >= 0 && iy2 < i32(pc.ny)) {
            let base = (pc.nx + 1u) * (pc.ny + 1u);
            let slot = base + u32(ix2) * pc.ny + u32(iy2);
            atomicAdd(&cnt[slot], 1u);
        }
    }
}
