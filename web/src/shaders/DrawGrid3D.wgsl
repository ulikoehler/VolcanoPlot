// DrawGrid3D.wgsl — ray-cast 3D grid planes (port of
// Grid3DRendererVk). Fullscreen triangle, no vertex buffer; the
// fragment shader unprojects near/far and intersects the grid planes.

struct GridUBO {
    rect : vec4f,        // xy = offset, zw = extent (unused, clip only)
    viewX : vec4f,       // x = min, z = span
    viewY : vec4f,
    viewZ : vec4f,
    gridColor : vec4f,
    flags : vec4f,       // x = floorXZ, y = backWallXY, z = sideWallYZ, w = step
    invVP0 : vec4f,
    invVP1 : vec4f,
    invVP2 : vec4f,
    invVP3 : vec4f,
    eye : vec4f,
};
@group(0) @binding(0) var<uniform> U : GridUBO;

struct VOut {
    @builtin(position) pos : vec4f,
    @location(0) ndc : vec2f,
};

@vertex fn vs(@builtin(vertex_index) vi : u32) -> VOut {
    // fullscreen triangle: (-1,-1) (3,-1) (-1,3)
    var p = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    var o : VOut;
    o.ndc = p[vi];
    o.pos = vec4f(p[vi], 0.0, 1.0);
    return o;
}

fn unproject(ndc : vec3f) -> vec3f {
    // invVP rows as stored; WGSL mat4x4f takes column vectors, so the
    // transposed construction below matches the GLSL port.
    let m = mat4x4f(
        vec4f(U.invVP0.x, U.invVP1.x, U.invVP2.x, U.invVP3.x),
        vec4f(U.invVP0.y, U.invVP1.y, U.invVP2.y, U.invVP3.y),
        vec4f(U.invVP0.z, U.invVP1.z, U.invVP2.z, U.invVP3.z),
        vec4f(U.invVP0.w, U.invVP1.w, U.invVP2.w, U.invVP3.w));
    let world = m * vec4f(ndc, 1.0);
    return world.xyz / world.w;
}

fn gridLine(coord : f32, step : f32) -> f32 {
    if (step <= 0.0) { return 0.0; }
    let f = abs(fract(coord / step - 0.5) - 0.5);
    let w = fwidth(coord / step);
    return 1.0 - smoothstep(0.0, w * 1.5, f);
}

@fragment fn fs(in : VOut) -> @location(0) vec4f {
    let nearPoint = unproject(vec3f(in.ndc, 0.0));
    let farPoint = unproject(vec3f(in.ndc, 1.0));
    let rayDir = farPoint - nearPoint;

    var step = U.flags.w;
    if (step <= 0.0) {
        let span = max(U.viewX.z, max(U.viewY.z, U.viewZ.z));
        step = pow(10.0, floor(log2(max(span, 1e-30)) / log2(10.0)));
        if (step <= 0.0) { step = 1.0; }
    }

    // Derivatives (fwidth inside gridLine) require uniform control
    // flow — evaluate every plane unconditionally and gate with select.
    var alpha = 0.0;

    // Floor plane: y = yMin
    {
        let t = (U.viewY.x - nearPoint.y) / rayDir.y;
        let p = nearPoint + t * rayDir;
        let ok = U.flags.x > 0.5 && abs(rayDir.y) > 1e-6 && t > 0.0 &&
                 p.x >= U.viewX.x && p.x <= U.viewX.x + U.viewX.z &&
                 p.z >= U.viewZ.x && p.z <= U.viewZ.x + U.viewZ.z;
        alpha = max(alpha, select(0.0,
            max(gridLine(p.x, step), gridLine(p.z, step)), ok));
    }
    // Back wall: z = zMin
    {
        let t = (U.viewZ.x - nearPoint.z) / rayDir.z;
        let p = nearPoint + t * rayDir;
        let ok = U.flags.y > 0.5 && abs(rayDir.z) > 1e-6 && t > 0.0 &&
                 p.x >= U.viewX.x && p.x <= U.viewX.x + U.viewX.z &&
                 p.y >= U.viewY.x && p.y <= U.viewY.x + U.viewY.z;
        alpha = max(alpha, select(0.0,
            max(gridLine(p.x, step), gridLine(p.y, step)), ok));
    }
    // Side wall: x = xMin
    {
        let t = (U.viewX.x - nearPoint.x) / rayDir.x;
        let p = nearPoint + t * rayDir;
        let ok = U.flags.z > 0.5 && abs(rayDir.x) > 1e-6 && t > 0.0 &&
                 p.y >= U.viewY.x && p.y <= U.viewY.x + U.viewY.z &&
                 p.z >= U.viewZ.x && p.z <= U.viewZ.x + U.viewZ.z;
        alpha = max(alpha, select(0.0,
            max(gridLine(p.y, step), gridLine(p.z, step)), ok));
    }

    if (alpha < 0.01) { discard; }
    return vec4f(U.gridColor.rgb, U.gridColor.a * alpha);
}
