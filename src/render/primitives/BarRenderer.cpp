// volcano/render/primitives/BarRenderer.cpp — stub implementation
#include "volcano/render/primitives/BarRenderer.hpp"
#include <volcano/plot/Transform.hpp>
#include <stdexcept>

namespace volcano::render::primitives {

void BarRenderer::init(vk::Device device, vk::RenderPass renderPass,
                       vk::SampleCountFlagBits samples, core::PipelineCache& cache) {
    // TODO: full bar pipeline with per-vertex color.
    (void)device; (void)renderPass; (void)samples; (void)cache;
    inited_ = true;
}

void BarRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                         VmaAllocator allocator, const plot::BarData& data) {
    // Build a quad per bar: 6 vertices (two triangles).
    std::vector<plot::Point2D> verts;
    verts.reserve(data.heights.size() * 6);
    float n = static_cast<float>(data.heights.size());
    float bw = data.width / n;
    for (size_t i = 0; i < data.heights.size(); ++i) {
        float x0 = i * bw + (1.0f - data.width) * 0.5f;
        float x1 = x0 + bw;
        float h = data.heights[i];
        plot::Point2D bl{x0, 0}, br{x1, 0}, tl{x0, h}, tr{x1, h};
        verts.insert(verts.end(), {bl, br, tl, br, tr, tl});
    }
    vertexCount_ = static_cast<uint32_t>(verts.size());
    core::BufferDesc d;
    d.size = verts.size() * sizeof(plot::Point2D);
    d.usage = core::BufferUsage::Vertex;
    vertexBuffer_ = core::Buffer(allocator, d);
    vertexBuffer_.upload(device, queue, pool,
                         std::as_bytes(std::span{verts.data(), verts.size()}));
}

void BarRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D /*rect*/,
                       const plot::Transform2D& /*transform*/) const {
    if (!inited_ || vertexCount_ == 0) return;
    // TODO: bind pipeline + push constants.
    (void)cmd;
}

} // namespace volcano::render::primitives
