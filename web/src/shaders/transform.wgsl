// web/src/shaders/transform.wgsl — WGSL port of
// src/render/shaders/TransformGlsl.hpp. Keep function-for-function
// parity; the field layout mirrors the native push-constant block.
//
// NOTE: GLSL mod() is floor-mod; WGSL % on floats is trunc-mod.
// Wherever the GLSL used mod(x, y) write `x - y*floor(x/y)` (helper
// `glslMod`). atan(y, x) → atan2(y, x).

struct Xform {
    viewMinSpan : vec4f,   // xy=display min, zw=display span
    rect        : vec4f,   // pixel rect (x, y, w, h)
    color       : vec4f,
    scaleX      : vec4f,   // (code, p1, p2, pad)
    scaleY      : vec4f,
    proj        : vec4f,   // (code, thetaOffset, thetaDir, pad)
    extra       : vec4f,   // u_width etc.
    extra2      : vec4f,
};

fn glslMod(x : f32, y : f32) -> f32 { return x - y * floor(x / y); }

// scaleFwd: s=(code,param1,param2); codes match plot::ScaleKind.
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

fn projFwd(p : vec2f, pr : vec3f) -> vec2f {
    let c = i32(pr.x + 0.5);
    if (c == 0) { return p; }                                   // rectilinear
    if (c == 1) {                                               // polar
        let th = pr.z * p.x + pr.y;
        return vec2f(p.y * cos(th), p.y * sin(th));
    }
    let lon = clamp(p.x, -3.14159265, 3.14159265);
    let lat = clamp(p.y, -1.5707963, 1.5707963);
    if (c == 2) {                                               // aitoff
        let al = acos(clamp(cos(lat) * cos(lon * 0.5), -1.0, 1.0));
        let sa = select(sin(al) / al, 1.0, abs(al) < 1e-7);
        return vec2f(2.0 * cos(lat) * sin(lon * 0.5) / sa, sin(lat) / sa);
    }
    if (c == 3) {                                               // hammer
        let z = sqrt(max(1.0 + cos(lat) * cos(lon * 0.5), 1e-12));
        return vec2f(2.8284271 * cos(lat) * sin(lon * 0.5) / z,
                     1.41421356 * sin(lat) / z);
    }
    if (c == 4) {                                               // lambert
        let k = sqrt(max(2.0 / (1.0 + cos(lat) * cos(lon)), 0.0));
        return vec2f(k * cos(lat) * sin(lon), k * sin(lat));
    }
    if (c == 5) {                                               // mollweide
        let s = clamp(sin(lat), -1.0, 1.0);
        var th = lat;
        for (var i = 0; i < 8; i++) {
            let f = 2.0 * th + sin(2.0 * th) - 3.14159265 * s;
            let fp = 2.0 + 2.0 * cos(2.0 * th);
            if (abs(fp) < 1e-12) { break; }
            th -= f / fp;
        }
        return vec2f(0.9003163 * lon * cos(th), 1.41421356 * sin(th));
    }
    return p;
}
