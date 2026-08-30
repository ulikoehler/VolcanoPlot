// volcano/plot/Triangulation.cpp — Delaunay triangulation (Bowyer-Watson)
#include "volcano/plot/Triangulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace volcano::plot {

namespace {

struct Circle {
    Point2D center;
    float radiusSq;
};

/// Compute the circumcircle of a triangle.
Circle circumcircle(Point2D a, Point2D b, Point2D c) {
    float d = 2.0f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-20f) {
        // Degenerate — return a huge circle.
        return {{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f},
                std::numeric_limits<float>::max()};
    }
    float ax2 = a.x * a.x + a.y * a.y;
    float bx2 = b.x * b.x + b.y * b.y;
    float cx2 = c.x * c.x + c.y * c.y;
    float ux = (ax2 * (b.y - c.y) + bx2 * (c.y - a.y) + cx2 * (a.y - b.y)) / d;
    float uy = (ax2 * (c.x - b.x) + bx2 * (a.x - c.x) + cx2 * (b.x - a.x)) / d;
    float dx = a.x - ux;
    float dy = a.y - uy;
    return {{ux, uy}, dx * dx + dy * dy};
}

/// Check if a point is inside a circumcircle.
bool inCircle(const Circle& circ, Point2D p) {
    float dx = p.x - circ.center.x;
    float dy = p.y - circ.center.y;
    return dx * dx + dy * dy < circ.radiusSq - 1e-10f;
}

/// A triangle with its circumcircle, used during construction.
struct TriNode {
    uint32_t a, b, c;
    Circle circ;
};

/// Edge of a triangle (directed).
struct Edge {
    uint32_t a, b;
    bool operator==(const Edge& o) const { return a == o.a && b == o.b; }
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

        // Find all triangles whose circumcircle contains p.
        std::vector<Edge> boundaryEdges;
        for (auto it = tris.begin(); it != tris.end(); ) {
            if (inCircle(it->circ, p)) {
                // Add this triangle's edges to the boundary.
                boundaryEdges.push_back({it->a, it->b});
                boundaryEdges.push_back({it->b, it->c});
                boundaryEdges.push_back({it->c, it->a});
                it = tris.erase(it);
            } else {
                ++it;
            }
        }

        // Remove duplicated edges (edges shared by two removed triangles).
        // An edge appears twice if it's shared; keep only unique edges.
        std::vector<Edge> uniqueEdges;
        for (size_t j = 0; j < boundaryEdges.size(); ++j) {
            bool shared = false;
            for (size_t k = j + 1; k < boundaryEdges.size(); ++k) {
                if ((boundaryEdges[j].a == boundaryEdges[k].a &&
                     boundaryEdges[j].b == boundaryEdges[k].b) ||
                    (boundaryEdges[j].a == boundaryEdges[k].b &&
                     boundaryEdges[j].b == boundaryEdges[k].a)) {
                    shared = true;
                    break;
                }
            }
            if (!shared)
                uniqueEdges.push_back(boundaryEdges[j]);
        }

        // Create new triangles connecting p to each unique boundary edge.
        for (const auto& e : uniqueEdges) {
            TriNode t;
            t.a = e.a;
            t.b = e.b;
            t.c = i;
            t.circ = circumcircle(allPoints[e.a], allPoints[e.b], p);
            tris.push_back(t);
        }
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
