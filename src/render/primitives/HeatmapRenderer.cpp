// volcano/render/primitives/HeatmapRenderer.cpp — stub
#include "volcano/render/primitives/HeatmapRenderer.hpp"
namespace volcano::render::primitives {
void HeatmapRenderer::init(vk::Device d, vk::RenderPass rp, vk::SampleCountFlagBits s,
                           core::PipelineCache& c, core::DescriptorPool& dp) {
    (void)d; (void)rp; (void)s; (void)c; (void)dp; inited_ = true;
}
void HeatmapRenderer::upload(vk::Device, vk::Queue, vk::CommandPool, VmaAllocator,
                             const plot::Grid2D&, const plot::Colormap&) {
    // TODO: upload grid as R32_SFLOAT image, colormap as RGBA8 1D LUT.
}
void HeatmapRenderer::draw(vk::CommandBuffer, vk::Rect2D, const plot::Transform2D&) const {
    // TODO: bind pipeline + sampled image.
}
} // namespace volcano::render::primitives
