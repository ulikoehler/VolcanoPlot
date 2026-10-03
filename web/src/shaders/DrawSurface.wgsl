// DrawSurface.wgsl — 3D surface mesh (port of SurfaceRendererVk).
// Vertex data lives in storage buffers pulled by index; the pipeline
// uses a depth attachment (unlike all 2D overlay pipelines).

struct SurfUBO {
    vp : mat4x4f,          // column-major view-projection
    gridRange : vec4f,     // xy = xRange min/max, zw = yRange min/max
    light : vec4f,         // xyz = light dir, w = shade flag
    valueRange : vec2f,    // min, max of z values
    gridDim : vec2u,       // vertex-pull grid dims (0 = indexed mesh)
};

@group(0) @binding(0) var<uniform> U : SurfUBO;
// C++ Point3D is 12 B — pull raw u32s, three per vertex.
@group(0) @binding(1) var<storage, read> verts : array<u32>;
@group(0) @binding(2) var<storage, read> indices : array<u32>;

struct VOut {
    @builtin(position) pos : vec4f,
    @location(0) height : f32,
    @location(1) world : vec3f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VOut {
#ifdef PULL_GRID
    // Vertex-pull grid: verts[] is a flat f32 z array; cell topology
    // comes from vertex_index. Per cell the indexed mesh walks
    // {a, c, b, b, c, d} — corner offsets in (i,j):
    //   a=(0,0) c=(0,1) b=(1,0)  b=(1,0) c=(0,1) d=(1,1)
    let gw = U.gridDim.x;
    let cell = vi / 6u;
    let corner = array<vec2u, 6>(
        vec2u(0, 0), vec2u(0, 1), vec2u(1, 0),
        vec2u(1, 0), vec2u(0, 1), vec2u(1, 1))[vi % 6u];
    let ci = vec2u(cell % (gw - 1u), cell / (gw - 1u)) + corner;
    let x = U.gridRange.x +
            f32(ci.x) / f32(gw - 1u) *
            (U.gridRange.y - U.gridRange.x);
    let y = U.gridRange.z +
            f32(ci.y) / f32(U.gridDim.y - 1u) *
            (U.gridRange.w - U.gridRange.z);
    let p = vec3f(x, y, bitcast<f32>(verts[ci.y * gw + ci.x]));
#else
    let idx = indices[vi];
    let p = vec3f(bitcast<f32>(verts[idx * 3u]),
                  bitcast<f32>(verts[idx * 3u + 1u]),
                  bitcast<f32>(verts[idx * 3u + 2u]));
#endif
    let nx = (p.x - U.gridRange.x) /
             max(U.gridRange.y - U.gridRange.x, 1e-30);
    let ny = (p.y - U.gridRange.z) /
             max(U.gridRange.w - U.gridRange.z, 1e-30);
    let h = (p.z - U.valueRange.x) /
            max(U.valueRange.y - U.valueRange.x, 1e-30);
    var o : VOut;
    o.height = h;
    o.world = vec3f(nx, ny, h);
    o.pos = U.vp * vec4f(p, 1.0);
    return o;
}

fn viridis(t : f32) -> vec3f {
    let c = array<vec3f, 11>(
        vec3f(0.267, 0.005, 0.329), vec3f(0.282, 0.140, 0.457),
        vec3f(0.254, 0.265, 0.530), vec3f(0.207, 0.372, 0.553),
        vec3f(0.164, 0.471, 0.558), vec3f(0.128, 0.567, 0.551),
        vec3f(0.135, 0.659, 0.518), vec3f(0.267, 0.749, 0.441),
        vec3f(0.478, 0.821, 0.318), vec3f(0.741, 0.873, 0.150),
        vec3f(0.993, 0.906, 0.144));
    let s = t * 10.0;
    let i = i32(s);
    let f = s - f32(i);
    return mix(c[clamp(i, 0, 10)], c[clamp(i + 1, 0, 10)], f);
}

@fragment fn fs(in : VOut) -> @location(0) vec4f {
    var color = viridis(clamp(in.height, 0.0, 1.0));
    if (U.light.w > 0.5) {
        // mpl plot_surface shade=True: lambert via screen-space normals.
        var n = cross(dpdx(in.world), dpdy(in.world));
        let nl = length(n);
        n = select(vec3f(0.0, 0.0, 1.0), n / nl, nl > 1e-12);
        if (n.z < 0.0) { n = -n; }
        let i = clamp(dot(n, normalize(U.light.xyz)), 0.0, 1.0);
        color = color * (0.30 + 0.70 * i) + vec3f(0.10) * i * i;
    }
    return vec4f(color, 1.0);
}
