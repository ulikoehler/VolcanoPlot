// volcano/text/TextRenderer.cpp
#include "volcano/text/TextRenderer.hpp"
namespace volcano::text {
void TextRenderer::init(vk::Device d, vk::RenderPass rp, vk::SampleCountFlagBits s,
                        core::PipelineCache& c, core::DescriptorPool& dp) {
    (void)d; (void)rp; (void)s; (void)c; (void)dp; inited_ = true;
}
void TextRenderer::setAtlas(std::shared_ptr<GlyphAtlas> a) { atlas_ = std::move(a); }
void TextRenderer::draw(vk::CommandBuffer, vk::Rect2D, std::string_view,
                        float, float, plot::Color, float) {
    // TODO: build vertex buffer of glyph quads, bind atlas, draw.
}
} // namespace volcano::text
