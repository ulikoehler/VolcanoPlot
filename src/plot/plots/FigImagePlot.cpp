// volcano/plot/plots/FigImagePlot.cpp — figure-level image overlay implementation
#include "volcano/plot/plots/FigImagePlot.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/backend/Backend.hpp"
#include <stdexcept>

namespace volcano::plot {

FigImagePlot::FigImagePlot(std::vector<uint32_t> pixels, uint32_t width,
                           uint32_t height, FigImageConfig config)
    : pixels_(std::move(pixels)), width_(width), height_(height),
      config_(std::move(config)) {
    if (pixels_.size() != width_ * height_)
        throw std::invalid_argument("FigImagePlot: pixels size must be width*height");
    if (width_ == 0 || height_ == 0)
        throw std::invalid_argument("FigImagePlot: image dimensions must be non-zero");
}

void FigImagePlot::buildGeometry() {
    fillPositions_.clear();
    fillColors_.clear();

    float s = config_.scale;
    float ox = static_cast<float>(config_.x);
    float oy = static_cast<float>(config_.y);

    for (uint32_t row = 0; row < height_; ++row) {
        for (uint32_t col = 0; col < width_; ++col) {
            uint32_t rgba = pixels_[row * width_ + col];
            // Unpack RGBA8 (little-endian: R, G, B, A).
            float r = static_cast<float>(rgba & 0xFF) / 255.0f;
            float g = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
            float b = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
            float a = static_cast<float>((rgba >> 24) & 0xFF) / 255.0f;
            if (a < 0.01f) continue;  // skip fully transparent pixels

            // Pixel coordinates in figure space (Y-down: row 0 at top).
            // Convert to Y-up for the renderer: y = figHeight - row - 1.
            // But since we set viewport to [0, figW, 0, figH] and the
            // vertex shader flips Y, we use Y-down pixel coords directly
            // and the viewport transform will handle the flip.
            // Actually, FillRenderer's vertex shader does:
            //   ndc = (p - viewMin) / viewSpan * 2 - 1, then y = -ndc.y
            // So if we set view = [0, figW, 0, figH] and use Y-up coords
            // (y = figH - row - 1), the flip makes row 0 at top. Correct.
            float x0 = ox + col * s;
            float x1 = ox + (col + 1) * s;
            // Y-up: bottom of pixel = figH - row - 1, top = figH - row.
            // But we don't know figH here. Instead, use Y-down coords
            // (row 0 at y=0) and set viewport y to [figH, 0] (inverted).
            // Actually, the simplest: use Y-down pixel coords and set
            // viewport to [0, figW, 0, figH]. The vertex shader flips Y,
            // so y=0 → bottom of screen, y=figH → top. That puts row 0
            // at bottom. We want row 0 at top, so use y = figH - row.
            // Since we don't know figH, we'll handle the flip in draw()
            // by setting an inverted viewport.
            // For now, store Y-down coords and flip in draw().
            float y0 = oy + row * s;
            float y1 = oy + (row + 1) * s;

            Point2D bl{x0, y0}, br{x1, y0}, tl{x0, y1}, tr{x1, y1};
            fillPositions_.push_back(bl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tl);
            fillPositions_.push_back(br);
            fillPositions_.push_back(tr);
            fillPositions_.push_back(tl);
            Color c{r, g, b, a};
            for (int k = 0; k < 6; ++k) fillColors_.push_back(c);
        }
    }
}

void FigImagePlot::prepare(render::Renderer& r) {
    buildGeometry();
    auto& ctx = r.backend().context();
    fillRenderer_.init(ctx.device.handle(), r.backend().renderPass(),
                       r.backend().sampleCount(), r.pipelineCache());
    if (!fillPositions_.empty()) {
        fillRenderer_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                             ctx.graphicsPool.handle(), ctx.allocator.handle(),
                             std::span{fillPositions_}, std::span{fillColors_});
    }
    prepared_ = true;
}

void FigImagePlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                        const Axes& axes, Rect2D rect) {
    if (!prepared_ || fillPositions_.empty()) return;

    // Use the full framebuffer extent for the scissor rect.
    auto ext = r.backend().extent();
    vk::Rect2D fullRect{vk::Offset2D{0, 0}, ext};

    // Set up a pixel-space transform. The FillRenderer vertex shader does:
    //   ndc = (p - viewMin) / viewSpan * 2 - 1
    //   gl_Position = vec4(ndc.x, -ndc.y, 0, 1)
    // Vulkan viewport is Y-down: NDC y=-1 → top, y=+1 → bottom.
    // So gl_Position.y = -ndc.y means:
    //   ndc.y = -1 → gl_Position.y = +1 → bottom of screen
    //   ndc.y = +1 → gl_Position.y = -1 → top of screen
    // We want row 0 (y=0) at the top. So we need ndc.y = +1 when y=0.
    // That means view.y should be [figH, 0] (inverted), so:
    //   ndc.y = (0 - figH) / (0 - figH) * 2 - 1 = 1  → top. Correct.
    //   ndc.y = (figH - figH) / (0 - figH) * 2 - 1 = -1  → bottom. Correct.
    Transform2D t;
    t.view.x = {0.0f, static_cast<float>(ext.width)};
    t.view.y = {static_cast<float>(ext.height), 0.0f};  // inverted Y
    t.view.z = {0, 1};
    t.logX = false;
    t.logY = false;

    fillRenderer_.draw(cmd, fullRect, t);
}

void FigImagePlot::contributeToAutoscale(Viewport& v) const {
    // FigImage is in pixel space — doesn't contribute to data autoscale.
}

} // namespace volcano::plot
