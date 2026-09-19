// volcano/plot/plots/Axes3DPlot.cpp — mplot3d-style axes box
#include "volcano/plot/plots/Axes3DPlot.hpp"
#include "volcano/plot/Ticks.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace volcano::plot {
namespace {

Point2D project(const std::array<float, 16>& vp, float x, float y, float z) {
    float cx = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float cy = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float cw = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (cw == 0.0f) cw = 1e-9f;
    // -ndcY: Vulkan NDC +y is down; the 2D renderers expect y-up data.
    return {cx / cw, -cy / cw};
}

} // namespace

Axes3DPlot::Axes3DPlot(Camera3D camera, Viewport dataRange,
                       Axes3DConfig config)
    : camera_(camera), range_(dataRange), config_(std::move(config)) {}

void Axes3DPlot::contributeToAutoscale(Viewport& v) const {
    v.x = {std::min(v.x.min, range_.x.min), std::max(v.x.max, range_.x.max)};
    v.y = {std::min(v.y.min, range_.y.min), std::max(v.y.max, range_.y.max)};
    v.z = {std::min(v.z.min, range_.z.min), std::max(v.z.max, range_.z.max)};
}

void Axes3DPlot::prepare(render::Renderer& r) {
    auto vp = camera_.viewProjection();

    float x0 = range_.x.min, x1 = range_.x.max;
    float y0 = range_.y.min, y1 = range_.y.max;
    float z0 = range_.z.min, z1 = range_.z.max;
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, cz = (z0 + z1) / 2;
    (void)cz;

    // Pane faces: the far side of each axis (opposite the camera eye).
    // With box normalization the eye is in box space centered at 0, so
    // sign(eye) selects the near face; the pane is the opposite face.
    float xp = camera_.eye.x > 0 ? x0 : x1;
    float yp = camera_.eye.y > 0 ? y0 : y1;
    float zp = camera_.eye.z > 0 ? z0 : z1;

    auto P = [&](float x, float y, float z) { return project(vp, x, y, z); };
    auto quad = [&](Point2D a, Point2D b, Point2D c, Point2D d) {
        paneTris_.insert(paneTris_.end(), {a, b, c, a, c, d});
    };

    // Pane fills (projected quads → two triangles each).
    if (config_.panes) {
        quad(P(xp, y0, z0), P(xp, y1, z0), P(xp, y1, z1), P(xp, y0, z1));
        quad(P(x0, yp, z0), P(x1, yp, z0), P(x1, yp, z1), P(x0, yp, z1));
        quad(P(x0, y0, zp), P(x1, y0, zp), P(x1, y1, zp), P(x0, y1, zp));
    }

    MaxNLocator loc(config_.tickBins);
    auto xt = loc.tickValues(x0, x1);
    auto yt = loc.tickValues(y0, y1);
    auto zt = loc.tickValues(z0, z1);
    auto clipT = [](std::vector<float>& ts, float lo, float hi) {
        std::erase_if(ts, [&](float t) { return t < lo - 1e-6f || t > hi + 1e-6f; });
    };
    clipT(xt, x0, x1); clipT(yt, y0, y1); clipT(zt, z0, z1);

    auto seg = [&](Point2D a, Point2D b) {
        lineSegs_.push_back(a); lineSegs_.push_back(b);
    };

    // Pane gridlines at tick positions.
    if (config_.grid) {
        for (float t : yt) { seg(P(xp, t, z0), P(xp, t, z1)); }
        for (float t : zt) { seg(P(xp, y0, t), P(xp, y1, t)); }
        for (float t : xt) { seg(P(t, yp, z0), P(t, yp, z1)); }
        for (float t : zt) { seg(P(x0, yp, t), P(x1, yp, t)); }
        for (float t : xt) { seg(P(t, y0, zp), P(t, y1, zp)); }
        for (float t : yt) { seg(P(x0, t, zp), P(x1, t, zp)); }
    }

    // 12 box edges.
    if (config_.edges) {
        for (float x : {x0, x1})
            for (float y : {y0, y1}) seg(P(x, y, z0), P(x, y, z1));
        for (float x : {x0, x1})
            for (float z : {z0, z1}) seg(P(x, y0, z), P(x, y1, z));
        for (float y : {y0, y1})
            for (float z : {z0, z1}) seg(P(x0, y, z), P(x1, y, z));
    }

    // Tick labels on the three outer axis edges:
    //   x axis: front-bottom edge (y = front, z = zmin)
    //   y axis: front-bottom edge (x = front, z = zmin)
    //   z axis: front vertical edge (x = front, y = front)
    // "front" is the near side for that axis.
    float yf = camera_.eye.y > 0 ? y1 : y0;
    float xf = camera_.eye.x > 0 ? x1 : x0;
    // mpl places the z axis on the rightmost vertical box edge.
    float zx = xf, zy = yf, bestX = -2.0f;
    for (float ex : {x0, x1})
        for (float ey : {y0, y1}) {
            float nx = P(ex, ey, cz).x;
            if (nx > bestX) { bestX = nx; zx = ex; zy = ey; }
        }
    // Box center in NDC — labels are pushed away from it.
    auto ctr = P(cx, cy, cz);
    auto outDir = [&](Point2D p) {
        float dx = p.x - ctr.x, dy = p.y - ctr.y;
        float n = std::sqrt(dx*dx + dy*dy);
        return n > 1e-9f ? Point2D{dx / n, dy / n} : Point2D{0, -1};
    };
    if (config_.tickLabels) {
        ScalarFormatter fmt;
        auto add = [&](Point2D p, float v) {
            auto d = outDir(p);
            labels_.push_back({p.x + d.x * 0.10f, p.y + d.y * 0.10f,
                               fmt.format(v, 0)});
        };
        for (float t : xt) add(P(t, yf, z0), t);
        for (float t : yt) add(P(xf, t, z0), t);
        for (float t : zt) add(P(zx, zy, t), t);
    }

    auto& ctx = r.backend().context();
    if (!paneTris_.empty()) {
        fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        std::vector<Color> cols(paneTris_.size(), config_.paneColor);
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{paneTris_}, std::span{cols});
    }
    if (!lineSegs_.empty()) {
        lineRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                           r.backend().sampleCount(), r.pipelineCache());
        lineRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{lineSegs_}, config_.edgeColor,
                             config_.lineWidth);
    }
    prepared_ = true;
}

void Axes3DPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                      const Axes&, Rect2D rect) {
    if (!prepared_) return;

    Transform2D t;
    t.view.x = {-1.0f, 1.0f};
    t.view.y = {-1.0f, 1.0f};
    t.view.z = {0, 1};
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};

    if (!paneTris_.empty())
        fillRenderer_.draw(cmd, vrect, t);
    if (!lineSegs_.empty())
        lineRenderer_.draw(cmd, vrect, t,
                           static_cast<uint32_t>(lineSegs_.size()));

    // Tick labels in pixel space. NDC (x, y-up) → pixel.
    if (config_.tickLabels && !labels_.empty()) {
        auto& text = r.textRenderer();
        auto ext = r.backend().extent();
        vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};
        for (const auto& l : labels_) {
            float px = rect.x + (l.x + 1.0f) * 0.5f * rect.width;
            float py = rect.y + (1.0f - (l.y + 1.0f) * 0.5f) * rect.height;
            auto m = text.measureText(l.text, 1.0f);
            text.draw(cmd, fullRect, l.text,
                      px - m.width * 0.5f, py - m.height * 0.5f,
                      config_.labelColor, 1.0f);
        }
    }
}

} // namespace volcano::plot
