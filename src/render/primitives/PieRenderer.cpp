// volcano/render/primitives/PieRenderer.cpp — stub
#include "volcano/render/primitives/PieRenderer.hpp"
#include <cmath>
namespace volcano::render::primitives {
void PieRenderer::init(vk::Device d, vk::RenderPass rp, vk::SampleCountFlagBits s, core::PipelineCache& c) {
    (void)d; (void)rp; (void)s; (void)c; inited_ = true;
}
void PieRenderer::upload(vk::Device device, vk::Queue queue, vk::CommandPool pool,
                         VmaAllocator allocator, const plot::PieData& data) {
    float total = 0; for (auto v : data.values) total += v;
    if (total <= 0) return;
    std::vector<plot::Point2D> verts;
    float a0 = 0;
    for (size_t i = 0; i < data.values.size(); ++i) {
        float a1 = a0 + 2.0f * 3.14159265f * data.values[i] / total;
        constexpr int kSeg = 32;
        float r0 = data.innerRadius;
        float r1 = 1.0f + data.explode * float(i);
        for (int s = 0; s < kSeg; ++s) {
            float ta0 = a0 + (a1 - a0) * s / kSeg;
            float ta1 = a0 + (a1 - a0) * (s + 1) / kSeg;
            plot::Point2D i0{r0*cosf(ta0), r0*sinf(ta0)};
            plot::Point2D i1{r0*cosf(ta1), r0*sinf(ta1)};
            plot::Point2D o0{r1*cosf(ta0), r1*sinf(ta0)};
            plot::Point2D o1{r1*cosf(ta1), r1*sinf(ta1)};
            verts.insert(verts.end(), {i0, i1, o0, i1, o1, o0});
        }
        a0 = a1;
    }
    vertexCount_ = static_cast<uint32_t>(verts.size());
    core::BufferDesc d;
    d.size = verts.size() * sizeof(plot::Point2D);
    d.usage = core::BufferUsage::Vertex;
    vertexBuffer_ = core::Buffer(allocator, d);
    vertexBuffer_.upload(device, queue, pool,
                         std::as_bytes(std::span{verts.data(), verts.size()}));
}
void PieRenderer::draw(vk::CommandBuffer cmd, vk::Rect2D rect) const {
    (void)cmd; (void)rect;
    // TODO: bind pipeline + draw.
}
} // namespace volcano::render::primitives
