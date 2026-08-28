// volcano/plot/Transform.cpp
#include "volcano/plot/Transform.hpp"

#include <cmath>

namespace volcano::plot {

namespace {

float applyLog(float v, bool log) { return log ? std::log10(std::max(v, 1e-30f)) : v; }
float unapplyLog(float v, bool log) { return log ? std::pow(10.0f, v) : v; }

} // namespace

Point2D Transform2D::toNdc(Point2D p) const noexcept {
    float x = applyLog(p.x, logX);
    float y = applyLog(p.y, logY);
    float nx = (x - view.x.min) / (view.x.span() != 0 ? view.x.span() : 1);
    float ny = (y - view.y.min) / (view.y.span() != 0 ? view.y.span() : 1);
    return { nx * 2.0f - 1.0f, ny * 2.0f - 1.0f };
}

Point2D Transform2D::fromNdc(Point2D p) const noexcept {
    float nx = (p.x + 1.0f) * 0.5f;
    float ny = (p.y + 1.0f) * 0.5f;
    float x = view.x.min + nx * view.x.span();
    float y = view.y.min + ny * view.y.span();
    return { unapplyLog(x, logX), unapplyLog(y, logY) };
}

namespace {

void normalize(Point3D& v) {
    float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len > 0) { v.x /= len; v.y /= len; v.z /= len; }
}

Point3D cross(const Point3D& a, const Point3D& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

} // namespace

std::array<float, 16> Camera3D::viewMatrix() const noexcept {
    Point3D f = { target.x - eye.x, target.y - eye.y, target.z - eye.z };
    normalize(f);
    Point3D s = cross(f, up); normalize(s);
    Point3D u = cross(s, f);
    // Row-major
    return {
        s.x, s.y, s.z, -(s.x*eye.x + s.y*eye.y + s.z*eye.z),
        u.x, u.y, u.z, -(u.x*eye.x + u.y*eye.y + u.z*eye.z),
        -f.x, -f.y, -f.z, (f.x*eye.x + f.y*eye.y + f.z*eye.z),
        0,0,0,1
    };
}

std::array<float, 16> Camera3D::projectionMatrix() const noexcept {
    float fovRad = fov * 3.14159265358979f / 180.0f;
    float f = 1.0f / std::tan(fovRad * 0.5f);
    return {
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, farZ / (farZ - nearZ), -(nearZ * farZ) / (farZ - nearZ),
        0, 0, 1, 0
    };
}

std::array<float, 16> Camera3D::viewProjection() const noexcept {
    auto v = viewMatrix();
    auto p = projectionMatrix();
    std::array<float, 16> out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += p[i*4+k] * v[k*4+j];
            out[i*4+j] = s;
        }
    return out;
}

} // namespace volcano::plot
