// volcano/render/shaders/TransformGlsl.hpp — shared GLSL for scale+projection
//
// Push-constant fields used by every data-space vertex shader:
//   vec4 u_viewMinSpan  — xy = display-space min, zw = display-space span
//   vec4 u_scaleX       — (code, param1, param2, unused)
//   vec4 u_scaleY       — (code, param1, param2, unused)
//   vec4 u_proj         — (code, thetaOffset, thetaDir, unused)
// Codes match volcano::plot::ScaleKind / ProjectionKind.
#pragma once

namespace volcano::render::shaders {

/// GLSL: forward-scale one component. `s` = (code, param1, param2).
inline constexpr const char* kScaleFn = R"(
float scaleFwd(float v, vec3 s) {
    int c = int(s.x + 0.5);
    if (c == 1) return log(max(v, 1e-30)) / log(10.0);          // log
    if (c == 2) {                                                // symlog
        float lt = s.y, ls = s.z;
        float a = abs(v);
        if (a <= lt) return ls * v;
        return sign(v) * lt * (ls + log(a / lt) / log(10.0));
    }
    if (c == 3) {                                                // logit
        float p = clamp(v, 1e-7, 1.0 - 1e-7);
        return log(p / (1.0 - p));
    }
    if (c == 4) {                                                // asinh
        float a = max(s.y, 1e-30);
        return a * asinh(v / a);
    }
    if (c == 5) {                                                // mercator (lat)
        float phi = clamp(v, -1.48442223, 1.48442223);
        return log(tan(0.7853981633974483 + phi * 0.5));
    }
    return v;                                                    // linear / function
}
)";

/// GLSL: joint (x,y) projection applied after per-axis scales.
/// `pr` = (code, thetaOffset, thetaDir).
inline constexpr const char* kProjFn = R"(
vec2 projFwd(vec2 p, vec3 pr) {
    int c = int(pr.x + 0.5);
    if (c == 0) return p;                                        // rectilinear
    if (c == 1) {                                                // polar (theta, r)
        float th = pr.z * p.x + pr.y;
        return vec2(p.y * cos(th), p.y * sin(th));
    }
    float lon = clamp(p.x, -3.14159265, 3.14159265);
    float lat = clamp(p.y, -1.5707963, 1.5707963);
    if (c == 2) {                                                // aitoff
        float al = acos(clamp(cos(lat) * cos(lon * 0.5), -1.0, 1.0));
        float sa = abs(al) < 1e-7 ? 1.0 : sin(al) / al;
        return vec2(2.0 * cos(lat) * sin(lon * 0.5) / sa, sin(lat) / sa);
    }
    if (c == 3) {                                                // hammer
        float z = sqrt(max(1.0 + cos(lat) * cos(lon * 0.5), 1e-12));
        return vec2(2.8284271 * cos(lat) * sin(lon * 0.5) / z,
                    1.41421356 * sin(lat) / z);
    }
    if (c == 4) {                                                // lambert
        float k = sqrt(max(2.0 / (1.0 + cos(lat) * cos(lon)), 0.0));
        return vec2(k * cos(lat) * sin(lon), k * sin(lat));
    }
    if (c == 5) {                                                // mollweide
        float s = clamp(sin(lat), -1.0, 1.0);
        float th = lat;
        for (int i = 0; i < 8; ++i) {
            float f = 2.0 * th + sin(2.0 * th) - 3.14159265 * s;
            float fp = 2.0 + 2.0 * cos(2.0 * th);
            if (abs(fp) < 1e-12) break;
            th -= f / fp;
        }
        return vec2(0.9003163 * lon * cos(th), 1.41421356 * sin(th));
    }
    return p;
}
)";

} // namespace volcano::render::shaders
