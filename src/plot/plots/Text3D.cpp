// volcano/plot/plots/Text3D.cpp — 3D text annotation implementation
#include "volcano/plot/plots/Text3D.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

namespace {

/// Project a 3D point through a 4x4 row-major matrix to 2D NDC.
/// Returns (ndcX, ndcY, clipW) — clipW < 0 means behind camera.
struct ProjResult { float x, y, w; };

ProjResult project3D(const std::array<float, 16>& vp, float x, float y, float z) {
    float clipX = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float clipY = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float clipW = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (std::abs(clipW) < 1e-30f) return {0, 0, 1e-30f};
    return {clipX / clipW, clipY / clipW, clipW};
}

} // namespace

Text3D::Text3D(std::vector<Text3DItem> items)
    : items_(std::move(items)) {}

Text3D::Text3D(float x, float y, float z, std::string text,
               Color color, float fontSize) {
    Text3DItem item;
    item.x = x;
    item.y = y;
    item.z = z;
    item.text = std::move(text);
    item.color = color;
    item.fontSize = fontSize;
    items_.push_back(std::move(item));
}

void Text3D::prepare(render::Renderer& r) {
    // Nothing to upload — text is drawn directly in draw().
    prepared_ = true;
}

void Text3D::draw(vk::CommandBuffer cmd, render::Renderer& r,
                  const Axes& axes, Rect2D rect) {
    if (!prepared_ || items_.empty()) return;

    auto& text = r.textRenderer();
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    auto vp = camera_.viewProjection();
    float halfW = static_cast<float>(ext.width) * 0.5f;
    float halfH = static_cast<float>(ext.height) * 0.5f;

    for (const auto& item : items_) {
        if (item.text.empty()) continue;

        ProjResult pr = project3D(vp, item.x, item.y, item.z);
        // Skip points behind the camera (|clipW| too small or point
        // projects outside clip space depth).
        if (std::abs(pr.w) < 1e-30f) continue;

        // Convert NDC [-1, 1] to pixel coordinates.
        // NDC x=-1 → pixel 0, x=1 → pixel width
        // NDC y=-1 → pixel height, y=1 → pixel 0 (Y-down in pixel space)
        float px = halfW + pr.x * halfW;
        float py = halfH - pr.y * halfH;

        // TextRenderer draws at pixel coords with Y-down.
        // The atlas is pre-rendered at 16px; scale adjusts the shaping
        // font size but the atlas only has 16px glyphs. We use scale=1.0
        // for reliable rendering and treat fontSize as informational.
        text.draw(cmd, fullRect, item.text, px, py, item.color, 1.0f, item.rotation);
    }
}

void Text3D::contributeToAutoscale(Viewport& v) const {
    for (const auto& item : items_) {
        v.x.min = std::min(v.x.min, item.x);
        v.x.max = std::max(v.x.max, item.x);
        v.y.min = std::min(v.y.min, item.y);
        v.y.max = std::max(v.y.max, item.y);
        v.z.min = std::min(v.z.min, item.z);
        v.z.max = std::max(v.z.max, item.z);
    }
}

} // namespace volcano::plot
