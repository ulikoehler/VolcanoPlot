// volcano/render/primitives/SurfaceRenderer.cpp — stub
#include "volcano/render/primitives/SurfaceRenderer.hpp"
namespace volcano::render::primitives {
void SurfaceRenderer::init(vk::Device d, vk::RenderPass rp, vk::SampleCountFlagBits s, core::PipelineCache& c) {
    (void)d; (void)rp; (void)s; (void)c; inited_ = true;
}
void SurfaceRenderer::upload(vk::Device, vk::Queue, vk::CommandPool, VmaAllocator, const plot::Grid2D&) {
    // TODO: build vertex grid + index list for triangle mesh.
}
void SurfaceRenderer::draw(vk::CommandBuffer, vk::Rect2D, const plot::Camera3D&) const {
    // TODO: bind pipeline + draw indexed.
}
} // namespace volcano::render::primitives
