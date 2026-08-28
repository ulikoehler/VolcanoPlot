// volcano/text/GlyphAtlas.cpp
#include "volcano/text/GlyphAtlas.hpp"
namespace volcano::text {
void GlyphAtlas::build(vk::Device, vk::Queue, vk::CommandPool, VmaAllocator,
                       const Font&, std::u32string_view) {
    // TODO: rasterize atlas, upload as R8 image, create view.
}
const GlyphInfo* GlyphAtlas::glyph(uint32_t) const { return nullptr; }
vk::ImageView GlyphAtlas::view() const noexcept { return {}; }
} // namespace volcano::text
