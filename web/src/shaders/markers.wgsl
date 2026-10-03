// markers.wgsl — matplotlib marker SDFs, shared by DrawPoints.wgsl and
// Draw3D.wgsl (prepended via the `markers` pipe flag; requires glslMod
// from transform.wgsl). Codes match the GLSL impl — subset for v1.
fn markerDist(c : vec2f, code : i32, nside : i32, rot : f32) -> f32 {
    // normalize to marker half-extents (~size/2 space is `local`)
    let r = c * 2.0;   // scale so radius-1 marker ≈ half quad
    switch code {
        case 0i:  { return length(r) - 0.30; }   // '.'
        case 2i:  {                                // 's' box
            let q = abs(r) - vec2f(0.75, 0.75);
            return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0);
        }
        case 3i, 5i, 6i, 7i, 8i, 18i, 19i, 20i, 21i: {
            // regular n-gon (iq sdPoly) — nside resolved host-side
            let n = f32(nside);
            let a = atan2(r.x, r.y) + rot + 3.14159265;
            let rr = 6.2831853 / n;
            let m = glslMod(a, rr) - rr * 0.5;
            return length(vec2f(cos(m), sin(m)) * length(r))
                   * cos(glslMod(a, rr) - rr * 0.5)
                   - 0.9 * cos(rr * 0.5);
        }
        case 15i: {                              // 'P' plus
            let q = abs(r);
            return min(min(q.x, q.y), length(r) - 0.5);
        }
        case 16i: {                              // 'X' cross
            let cr = vec2f(r.x + r.y, r.x - r.y) * 0.7071;
            let q = abs(cr);
            return min(min(q.x, q.y), length(cr) - 0.5);
        }
        default: { return length(r) - 1.0; }     // 'o' circle + rest
    }
}
