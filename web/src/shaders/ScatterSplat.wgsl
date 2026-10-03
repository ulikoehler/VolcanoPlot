// web/src/shaders/ScatterSplat.wgsl — datashader-style density splatting
// (op 55). Two dispatches, driven by the interpreter:
//
//   `splat` — one invocation per point: atomicAdd into a u32 density
//             grid over the disc of `radius` px around the point.
//   `pack`  — one invocation per grid cell: clamp the count and write it
//             as f32 into a row-padded buffer that is copied into an
//             r32float texture (drawn through the colormap image path).
//
// Opt-in: overdraw becomes density, so the picture differs from the
// marker path by design.
//
//   0 uniform pc { n, W, H, rowStride, radius, maxDensity, nanValue,
//                  bx, ax, by, ay }
//   1 storage xy    : array<f32>            (x, y pairs, data space)
//   2 storage dens  : array<atomic<u32>>
//   3 storage packed : array<f32>           (rowStride per row)

struct SplatPC {
    n : u32, W : u32, H : u32, rowStride : u32,
    radius : f32, maxDensity : f32, nanValue : f32,
    bx : f32, ax : f32, by : f32, ay : f32,
};

@group(0) @binding(0) var<uniform> pc : SplatPC;
@group(0) @binding(1) var<storage, read> xy : array<f32>;
@group(0) @binding(2) var<storage, read_write> dens : array<atomic<u32>>;
@group(0) @binding(3) var<storage, read_write> packed : array<f32>;

fn deposit(cx : i32, cy : i32) {
    if (cx < 0 || cy < 0 || cx >= i32(pc.W) || cy >= i32(pc.H)) { return; }
    atomicAdd(&dens[u32(cy) * pc.W + u32(cx)], 1u);
}

@compute @workgroup_size(64)
fn splat(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    if (i >= pc.n) { return; }
    let x = pc.bx + xy[i * 2u] * pc.ax;
    let y = pc.by + xy[i * 2u + 1u] * pc.ay;
    if (!(x == x) || !(y == y)) { return; }
    let r = max(pc.radius, 0.5);
    let ri = i32(ceil(r));
    let cx0 = i32(floor(x));
    let cy0 = i32(floor(y));
    for (var dy = -ri; dy <= ri; dy = dy + 1) {
        for (var dx = -ri; dx <= ri; dx = dx + 1) {
            let fx = f32(dx) + 0.5;
            let fy = f32(dy) + 0.5;
            if (fx * fx + fy * fy > r * r) { continue; }
            deposit(cx0 + dx, cy0 + dy);
        }
    }
}

@compute @workgroup_size(64)
fn pack(@builtin(global_invocation_id) gid : vec3u) {
    let i = gid.x;
    let cells = pc.W * pc.H;
    if (i >= cells) { return; }
    let y = i / pc.W;
    let x = i % pc.W;
    let c = f32(atomicLoad(&dens[i]));
    // Empty cells stay transparent: the image path discards NaNs, so the
    // density cloud keeps its silhouette instead of a filled rectangle.
    if (c == 0.0) {
        // NaN (supplied by the host — WGSL has no NaN literal) makes the
        // image path discard the cell.
        packed[y * pc.rowStride + x] = pc.nanValue;
        return;
    }
    packed[y * pc.rowStride + x] = min(c, pc.maxDensity);
}
