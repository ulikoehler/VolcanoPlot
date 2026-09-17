// volcano/plot/Path.hpp — matplotlib-style Path (vertices + codes)
#pragma once

#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"

#include <span>
#include <utility>
#include <vector>

namespace volcano::plot {

/// A vector path: vertices plus movement codes (matplotlib `Path`).
/// Curves are cubic/quadratic Bézier; CLOSEPOLY closes a subpath.
class Path {
public:
    /// mpl-compatible code values.
    enum Code : uint8_t {
        Stop = 0,
        MoveTo = 1,
        LineTo = 2,
        Curve3 = 3,   // quadratic Bézier: control, end
        Curve4 = 4,   // cubic Bézier: ctrl1, ctrl2, end
        ClosePoly = 79,
    };

    std::vector<Point2D> vertices;
    std::vector<Code> codes;

    Path() = default;
    explicit Path(std::vector<Point2D> verts) : vertices(std::move(verts)) {
        codes.assign(vertices.size(), LineTo);
        if (!codes.empty()) codes[0] = MoveTo;
    }
    Path(std::vector<Point2D> verts, std::vector<Code> c)
        : vertices(std::move(verts)), codes(std::move(c)) {}

    // ── construction helpers ──
    void moveTo(Point2D p) { vertices.push_back(p); codes.push_back(MoveTo); }
    void lineTo(Point2D p) { vertices.push_back(p); codes.push_back(LineTo); }
    void curve3(Point2D ctrl, Point2D end) {
        vertices.push_back(ctrl); codes.push_back(Curve3);
        vertices.push_back(end);  codes.push_back(Curve3);
    }
    void curve4(Point2D c1, Point2D c2, Point2D end) {
        vertices.push_back(c1);  codes.push_back(Curve4);
        vertices.push_back(c2);  codes.push_back(Curve4);
        vertices.push_back(end); codes.push_back(Curve4);
    }
    void close() { vertices.push_back({0, 0}); codes.push_back(ClosePoly); }

    /// One flattened subpath (curves subdivided).
    struct Subpath {
        std::vector<Point2D> points;
        bool closed = false;
    };

    /// Flatten into line subpaths. `curveSteps` subdivisions per curve.
    [[nodiscard]] std::vector<Subpath> toPolylines(int curveSteps = 16) const;
    /// All flattened points as one polyline soup (subpaths concatenated;
    /// NaN separators mark subpath breaks).
    [[nodiscard]] std::vector<Point2D> flatten(int curveSteps = 16) const;

    /// (min, max) vertex bounds (ignores codes).
    [[nodiscard]] std::pair<Point2D, Point2D> bounds() const;
    /// Even-odd point containment test on the flattened path.
    [[nodiscard]] bool containsPoint(Point2D p, int curveSteps = 32) const;
    /// Apply a transform to every vertex.
    [[nodiscard]] Path transformed(const Transform& t) const;

    // ── unit shapes (mpl Path.unit_*) ──
    /// Unit circle centered at origin, radius 1 (16-point polygonal approx
    /// refined by curveSteps at draw time — here a dense polygon).
    [[nodiscard]] static Path unitCircle();
    [[nodiscard]] static Path unitRectangle();
    /// Regular n-sided polygon, radius 1, first vertex at +x.
    [[nodiscard]] static Path unitRegularPolygon(int n);
    /// n-pointed star (alternating outer/inner radius 1 / 0.381966).
    [[nodiscard]] static Path unitStar(int n);
    /// n-armed asterisk (line spokes through the origin).
    [[nodiscard]] static Path unitAsterisk(int n);
    /// Unit wedge (annular sector): radius 1, angles in degrees.
    [[nodiscard]] static Path unitWedge(float theta1Deg, float theta2Deg,
                                        float innerR = 0.0f);
    /// mpl rectangle helper: (x, y, w, h).
    [[nodiscard]] static Path rectangle(float x, float y, float w, float h);
    /// Circle/ellipse outline: center, semi-axes, rotation degrees.
    [[nodiscard]] static Path ellipse(Point2D center, float rx, float ry,
                                      float angleDeg = 0.0f);
};

/// Triangulate a simple (non-self-intersecting) polygon by ear clipping.
/// `ring` is a closed or open point list (closing vertex optional).
/// Returns triangle soup vertices (3 per tri), empty on failure.
[[nodiscard]] std::vector<Point2D> earClip(std::span<const Point2D> ring);

/// Clip a line segment to a polygon; returns the inside sub-segments.
[[nodiscard]] std::vector<std::pair<Point2D, Point2D>>
clipSegmentToPolygon(Point2D a, Point2D b, std::span<const Point2D> poly);

} // namespace volcano::plot
