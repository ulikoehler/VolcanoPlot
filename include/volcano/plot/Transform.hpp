// volcano/plot/Transform.hpp — data↔screen coordinate transforms
#pragma once

#include "volcano/plot/Types.hpp"

#include <array>

namespace volcano::plot {

/// Maps data coordinates to normalized device coordinates (NDC, [-1,1]).
/// Used by the vertex shader push-constants/uniform.
struct Transform2D {
    Viewport view;
    /// If true, apply log10 to the respective axis before mapping.
    bool logX = false;
    bool logY = false;

    [[nodiscard]] Point2D toNdc(Point2D p) const noexcept;
    [[nodiscard]] Point2D fromNdc(Point2D p) const noexcept;
};

/// Camera for 3D plots (orbit + perspective).
struct Camera3D {
    Point3D eye{2,2,2};
    Point3D target{0,0,0};
    Point3D up{0,0,1};
    float fov = 45.0f;       // degrees
    float aspect = 1.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;

    /// Compute view matrix (row-major 4x4).
    [[nodiscard]] std::array<float, 16> viewMatrix() const noexcept;
    /// Compute projection matrix (row-major 4x4).
    [[nodiscard]] std::array<float, 16> projectionMatrix() const noexcept;
    /// Combined view-projection.
    [[nodiscard]] std::array<float, 16> viewProjection() const noexcept;
};

} // namespace volcano::plot
