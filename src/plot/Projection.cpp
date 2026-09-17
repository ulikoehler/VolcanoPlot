// volcano/plot/Projection.cpp
#include "volcano/plot/Projection.hpp"

#include <algorithm>
#include <cmath>

namespace volcano::plot {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrt2 = 1.4142135623730951f;

/// Mollweide auxiliary angle: solve 2θ + sin2θ = π·sinφ by Newton iteration.
float mollweideTheta(float lat) noexcept {
    float s = std::clamp(std::sin(lat), -1.0f, 1.0f);
    float th = lat;
    for (int i = 0; i < 8; ++i) {
        float f = 2.0f * th + std::sin(2.0f * th) - kPi * s;
        float fp = 2.0f + 2.0f * std::cos(2.0f * th);
        if (std::fabs(fp) < 1e-12f) break;
        th -= f / fp;
    }
    return th;
}
} // namespace

Point2D Projection::forward(Point2D p) const noexcept {
    switch (kind) {
    case ProjectionKind::Polar: {
        float th = thetaDir * p.x + thetaOffset;
        return {p.y * std::cos(th), p.y * std::sin(th)};
    }
    case ProjectionKind::Aitoff: {
        // p = (lon, lat) in radians.
        float lon = std::clamp(p.x, -kPi, kPi);
        float lat = std::clamp(p.y, -kPi / 2.0f, kPi / 2.0f);
        float c = std::cos(lat) * std::cos(lon * 0.5f);
        float alpha = std::acos(std::clamp(c, -1.0f, 1.0f));
        float sa = std::fabs(alpha) < 1e-7f ? 1.0f : std::sin(alpha) / alpha;
        return {2.0f * std::cos(lat) * std::sin(lon * 0.5f) / sa,
                std::sin(lat) / sa};
    }
    case ProjectionKind::Hammer: {
        float lon = std::clamp(p.x, -kPi, kPi);
        float lat = std::clamp(p.y, -kPi / 2.0f, kPi / 2.0f);
        float z = std::sqrt(std::max(1.0f + std::cos(lat) * std::cos(lon * 0.5f),
                                     1e-12f));
        return {2.0f * kSqrt2 * std::cos(lat) * std::sin(lon * 0.5f) / z,
                kSqrt2 * std::sin(lat) / z};
    }
    case ProjectionKind::Lambert: {
        // Azimuthal equal-area centered at (0,0).
        float lon = std::clamp(p.x, -kPi, kPi);
        float lat = std::clamp(p.y, -kPi / 2.0f, kPi / 2.0f);
        float k = std::sqrt(
            std::max(2.0f / (1.0f + std::cos(lat) * std::cos(lon)), 0.0f));
        return {k * std::cos(lat) * std::sin(lon), k * std::sin(lat)};
    }
    case ProjectionKind::Mollweide: {
        float lon = std::clamp(p.x, -kPi, kPi);
        float lat = std::clamp(p.y, -kPi / 2.0f, kPi / 2.0f);
        float th = mollweideTheta(lat);
        return {2.0f * kSqrt2 / kPi * lon * std::cos(th),
                kSqrt2 * std::sin(th)};
    }
    case ProjectionKind::Rectilinear:
    default:
        return p;
    }
}

Point2D Projection::inverse(Point2D p) const noexcept {
    switch (kind) {
    case ProjectionKind::Polar: {
        float r = std::sqrt(p.x * p.x + p.y * p.y);
        float th = (std::atan2(p.y, p.x) - thetaOffset) * thetaDir;
        return {th, r};
    }
    case ProjectionKind::Aitoff: {
        // Solve by Newton on forward map.
        float lon = p.x, lat = p.y;
        Projection pr{ProjectionKind::Aitoff, 0, 1};
        for (int i = 0; i < 12; ++i) {
            auto f = pr.forward({lon, lat});
            float ex = f.x - p.x, ey = f.y - p.y;
            if (std::fabs(ex) + std::fabs(ey) < 1e-7f) break;
            constexpr float h = 1e-5f;
            auto fx = pr.forward({lon + h, lat});
            auto fy = pr.forward({lon, lat + h});
            float j00 = (fx.x - f.x) / h, j01 = (fy.x - f.x) / h;
            float j10 = (fx.y - f.y) / h, j11 = (fy.y - f.y) / h;
            float det = j00 * j11 - j01 * j10;
            if (std::fabs(det) < 1e-12f) break;
            lon -= (j11 * ex - j01 * ey) / det;
            lat -= (-j10 * ex + j00 * ey) / det;
            lon = std::clamp(lon, -kPi, kPi);
            lat = std::clamp(lat, -kPi / 2.0f, kPi / 2.0f);
        }
        return {lon, lat};
    }
    case ProjectionKind::Hammer: {
        float z = std::sqrt(std::max(
            1.0f - (p.x / 4.0f) * (p.x / 4.0f) - (p.y / 2.0f) * (p.y / 2.0f) * 0.5f,
            0.0f));
        // Hammer inverse: lon = 2 atan2(z x, 2(2z²-1)), lat = asin(z y)
        float lon = 2.0f * std::atan2(z * p.x, 2.0f * (2.0f * z * z - 1.0f));
        float lat = std::asin(std::clamp(z * p.y, -1.0f, 1.0f));
        return {lon, lat};
    }
    case ProjectionKind::Lambert: {
        float rho = std::sqrt(p.x * p.x + p.y * p.y);
        if (rho < 1e-12f) return {0, 0};
        float c = 2.0f * std::asin(std::clamp(rho / 2.0f, -1.0f, 1.0f));
        float lat = std::asin(std::clamp(p.y * std::sin(c) / rho, -1.0f, 1.0f));
        float lon = std::atan2(p.x * std::sin(c), rho * std::cos(c));
        return {lon, lat};
    }
    case ProjectionKind::Mollweide: {
        float th = std::asin(std::clamp(p.y / kSqrt2, -1.0f, 1.0f));
        float lat = std::asin(std::clamp(
            (2.0f * th + std::sin(2.0f * th)) / kPi, -1.0f, 1.0f));
        float ct = std::cos(th);
        float lon = std::fabs(ct) < 1e-9f
                        ? 0.0f
                        : p.x * kPi / (2.0f * kSqrt2 * ct);
        return {lon, lat};
    }
    case ProjectionKind::Rectilinear:
    default:
        return p;
    }
}

Viewport Projection::bounds(float rmax) const noexcept {
    Viewport v;
    switch (kind) {
    case ProjectionKind::Polar:
        v.x = {-rmax, rmax};
        v.y = {-rmax, rmax};
        break;
    case ProjectionKind::Aitoff:
        // forward(±π, 0) → (±π, 0) and forward(0, ±π/2) → (0, ±π/2):
        // the ellipse spans x ∈ [-π, π], y ∈ [-π/2, π/2].
        v.x = {-kPi, kPi};
        v.y = {-kPi / 2.0f, kPi / 2.0f};
        break;
    case ProjectionKind::Hammer:
        v.x = {-2.0f * kSqrt2, 2.0f * kSqrt2};
        v.y = {-kSqrt2, kSqrt2};
        break;
    case ProjectionKind::Lambert:
        v.x = {-2.0f, 2.0f};
        v.y = {-2.0f, 2.0f};
        break;
    case ProjectionKind::Mollweide:
        v.x = {-2.0f * kSqrt2, 2.0f * kSqrt2};
        v.y = {-kSqrt2, kSqrt2};
        break;
    case ProjectionKind::Rectilinear:
    default:
        break;
    }
    return v;
}

Projection Projection::parse(std::string_view name) {
    if (name == "polar") return {ProjectionKind::Polar, 0, 1};
    if (name == "aitoff") return {ProjectionKind::Aitoff, 0, 1};
    if (name == "hammer") return {ProjectionKind::Hammer, 0, 1};
    if (name == "lambert") return {ProjectionKind::Lambert, 0, 1};
    if (name == "mollweide") return {ProjectionKind::Mollweide, 0, 1};
    return {};
}

std::string_view Projection::name() const noexcept {
    switch (kind) {
    case ProjectionKind::Polar: return "polar";
    case ProjectionKind::Aitoff: return "aitoff";
    case ProjectionKind::Hammer: return "hammer";
    case ProjectionKind::Lambert: return "lambert";
    case ProjectionKind::Mollweide: return "mollweide";
    default: return "rectilinear";
    }
}

} // namespace volcano::plot
