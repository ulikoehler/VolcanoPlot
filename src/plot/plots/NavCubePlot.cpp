// volcano/plot/plots/NavCubePlot.cpp — 3D orientation indicator implementation
#include "volcano/plot/plots/NavCubePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace volcano::plot {

namespace {

/// Project a direction vector through the camera's view rotation only.
/// Returns screen-space (x, y, depth): x = right, y = up, depth = into
/// screen (positive = farther from viewer).
Point3D projectDir(const std::array<float, 16>& view, Point3D d) {
    return {
        view[0] * d.x + view[1] * d.y + view[2] * d.z,   // right
        view[4] * d.x + view[5] * d.y + view[6] * d.z,   // up
        view[8] * d.x + view[9] * d.y + view[10] * d.z   // -forward
    };
}

} // namespace

NavCubePlot::NavCubePlot(NavCubeConfig config)
    : config_(std::move(config)) {}

void NavCubePlot::prepare(render::Renderer&) {
    // Nothing to upload — drawn in pixel space via SpineRenderer scratch.
    prepared_ = true;
}

void NavCubePlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes&, Rect2D rect) {
    if (!prepared_) return;

    const auto& cfg = config_;
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
    auto& spine = r.spineRenderer();

    // Indicator center in pixels (Y-down).
    float c = cfg.size + cfg.margin;
    float cx = (cfg.corner == NavCubeCorner::UpperLeft ||
                cfg.corner == NavCubeCorner::LowerLeft)
             ? float(rect.x) + c
             : float(rect.x + rect.width) - c;
    float cy = (cfg.corner == NavCubeCorner::UpperLeft ||
                cfg.corner == NavCubeCorner::UpperRight)
             ? float(rect.y) + c
             : float(rect.y + rect.height) - c;

    auto view = cfg.camera.viewMatrix();
    auto toPixel = [&](Point3D d) -> Point2D {
        return { cx + d.x * cfg.size, cy - d.y * cfg.size };
    };

    // --- Cube wireframe (Cube mode): 12 edges of a unit cube. ---
    if (cfg.mode == NavCubeMode::Cube) {
        constexpr float h = 0.6f;  // cube half-extent in axis units
        const Point3D corners[8] = {
            {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
            {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
        };
        const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},  // back face
            {4,5},{5,6},{6,7},{7,4},  // front face
            {0,4},{1,5},{2,6},{3,7},  // connecting edges
        };
        Point2D px[8];
        float depth[8];
        for (int i = 0; i < 8; ++i) {
            Point3D p = projectDir(view, corners[i]);
            px[i] = toPixel(p);
            depth[i] = p.z;
        }
        // Sort edges far→near so nearer edges draw on top.
        std::array<int, 12> order;
        for (int i = 0; i < 12; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return depth[edges[a][0]] + depth[edges[a][1]]
                 > depth[edges[b][0]] + depth[edges[b][1]];
        });
        for (int ei : order) {
            Point2D pts[] = { px[edges[ei][0]], px[edges[ei][1]] };
            spine.drawLineStrip(cmd, fullRect, r.backend().extent(),
                                pts, cfg.dimColor, 1.0f);
        }
    }

    // --- Axis arrows: +X/+Y/+Z colored, -X/-Y/-Z dimmed (Triad mode). ---
    struct Axis { Point3D dir; Color color; const char* label; float depth; };
    std::array<Axis, 6> axes = {{
        { { 1,0,0}, cfg.xColor,   "x", 0 },
        { { 0,1,0}, cfg.yColor,   "y", 0 },
        { { 0,0,1}, cfg.zColor,   "z", 0 },
        { {-1,0,0}, cfg.dimColor, nullptr, 0 },
        { {0,-1,0}, cfg.dimColor, nullptr, 0 },
        { {0,0,-1}, cfg.dimColor, nullptr, 0 },
    }};
    size_t numAxes = cfg.showNegativeAxes ? 6 : 3;

    Point2D origin = {cx, cy};
    for (size_t i = 0; i < numAxes; ++i) {
        Point3D p = projectDir(view, axes[i].dir);
        axes[i].depth = p.z;
        axes[i].dir = { p.x, p.y, p.z };  // store projected direction
    }
    // Draw far→near.
    std::array<size_t, 6> ord{0,1,2,3,4,5};
    std::sort(ord.begin(), ord.begin() + numAxes, [&](size_t a, size_t b) {
        return axes[a].depth > axes[b].depth;
    });

    auto& text = r.textRenderer();
    for (size_t i = 0; i < numAxes; ++i) {
        const auto& ax = axes[ord[i]];
        Point2D tip = toPixel(ax.dir);
        Point2D pts[] = { origin, tip };
        spine.drawLineStrip(cmd, fullRect, r.backend().extent(),
                            pts, ax.color, cfg.axisWidth);

        // Label at the tip, offset slightly outward.
        if (cfg.showLabels && ax.label) {
            float dx = tip.x - cx, dy = tip.y - cy;
            float len = std::sqrt(dx*dx + dy*dy);
            float lx = tip.x, ly = tip.y;
            if (len > 1e-3f) { lx += dx / len * 4.0f; ly += dy / len * 4.0f; }
            text.draw(cmd, fullRect, ax.label, lx - 3.0f, ly + 4.0f,
                      ax.color, 0.8f);
        }
    }
}

} // namespace volcano::plot
