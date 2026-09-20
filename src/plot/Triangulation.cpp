// volcano/plot/Triangulation.cpp — Delaunay triangulation (Bowyer-Watson)
#include "volcano/plot/Triangulation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace volcano::plot {

namespace {

struct Circle {
    double cx, cy;
    double radiusSq;
};

/// Compute the circumcircle of a triangle (double precision — float32
/// degenerates on near-collinear triples and poisons every subsequent
/// inCircle test).
Circle circumcircle(Point2D a, Point2D b, Point2D c) {
    double d = 2.0 * (double(a.x) * (double(b.y) - double(c.y)) +
                      double(b.x) * (double(c.y) - double(a.y)) +
                      double(c.x) * (double(a.y) - double(b.y)));
    if (std::abs(d) < 1e-18) {
        // Degenerate (collinear) — give it a zero-radius circle so no
        // point is ever "inside" it.
        return {double(a.x), double(a.y), -1.0};
    }
    double ax2 = double(a.x) * a.x + double(a.y) * a.y;
    double bx2 = double(b.x) * b.x + double(b.y) * b.y;
    double cx2 = double(c.x) * c.x + double(c.y) * c.y;
    double ux = (ax2 * (double(b.y) - double(c.y)) +
                 bx2 * (double(c.y) - double(a.y)) +
                 cx2 * (double(a.y) - double(b.y))) / d;
    double uy = (ax2 * (double(c.x) - double(b.x)) +
                 bx2 * (double(a.x) - double(c.x)) +
                 cx2 * (double(b.x) - double(a.x))) / d;
    double dx = double(a.x) - ux;
    double dy = double(a.y) - uy;
    return {ux, uy, dx * dx + dy * dy};
}

/// Check if a point is inside a circumcircle.
bool inCircle(const Circle& circ, Point2D p) {
    if (circ.radiusSq < 0.0) return false;  // degenerate triangle
    double dx = double(p.x) - circ.cx;
    double dy = double(p.y) - circ.cy;
    double d2 = dx * dx + dy * dy;
    // Relative epsilon: strictly inside, not on the boundary.
    return d2 < circ.radiusSq * (1.0 - 1e-9);
}

/// A triangle with its circumcircle, used during construction.
struct TriNode {
    uint32_t a, b, c;
    Circle circ;
};

} // namespace

std::vector<Triangle> delaunay(const std::vector<Point2D>& points) {
    if (points.size() < 3) return {};

    // Compute bounding box.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const auto& p : points) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    float dx = maxX - minX;
    float dy = maxY - minY;
    if (dx < 1e-10f) dx = 1.0f;
    if (dy < 1e-10f) dy = 1.0f;
    float delta = std::max(dx, dy) * 10.0f;
    float midX = (minX + maxX) * 0.5f;
    float midY = (minY + maxY) * 0.5f;

    // Super-triangle vertices (indices: N, N+1, N+2 where N = points.size()).
    uint32_t N = static_cast<uint32_t>(points.size());
    std::vector<Point2D> allPoints(points);
    allPoints.push_back({midX - delta, midY - delta});
    allPoints.push_back({midX + delta, midY - delta});
    allPoints.push_back({midX, midY + delta});

    // Initial triangulation: just the super-triangle.
    std::vector<TriNode> tris;
    tris.push_back({N, N + 1, N + 2,
                    circumcircle(allPoints[N], allPoints[N + 1], allPoints[N + 2])});

    // Incrementally insert each point.
    for (uint32_t i = 0; i < N; ++i) {
        const Point2D& p = allPoints[i];

        // Find all triangles whose circumcircle contains p (the cavity).
        // Collect boundary edges as normalized (lo, hi) pairs so shared
        // edges can be removed with one sort instead of an O(E²) scan.
        std::vector<uint64_t> edges;
        std::vector<TriNode> kept;
        kept.reserve(tris.size());
        for (auto& t : tris) {
            if (inCircle(t.circ, p)) {
                uint32_t vs[3] = {t.a, t.b, t.c};
                for (int e = 0; e < 3; ++e) {
                    uint32_t a = vs[e], b = vs[(e + 1) % 3];
                    uint32_t lo = std::min(a, b), hi = std::max(a, b);
                    edges.push_back((uint64_t(lo) << 32) | hi);
                }
            } else {
                kept.push_back(t);
            }
        }
        tris = std::move(kept);

        // Sort edge keys; edges appearing exactly once are cavity
        // boundary (shared edges cancel in pairs).
        std::sort(edges.begin(), edges.end());
        std::vector<TriNode> newTris;
        for (size_t j = 0; j < edges.size();) {
            size_t k = j;
            while (k < edges.size() && edges[k] == edges[j]) ++k;
            if (k - j == 1) {
                uint32_t a = uint32_t(edges[j] >> 32);
                uint32_t b = uint32_t(edges[j] & 0xFFFFFFFFu);
                TriNode t{a, b, i,
                          circumcircle(allPoints[a], allPoints[b], p)};
                newTris.push_back(t);
            }
            j = k;
        }
        for (auto& t : newTris) tris.push_back(t);
    }

    // Remove triangles that share a vertex with the super-triangle.
    std::vector<Triangle> result;
    result.reserve(tris.size());
    for (const auto& t : tris) {
        if (t.a >= N || t.b >= N || t.c >= N) continue;
        result.push_back({t.a, t.b, t.c});
    }
    return result;
}

} // namespace volcano::plot
