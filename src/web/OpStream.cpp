// src/web/OpStream.cpp
#include "OpStream.hpp"

namespace volcano::web {

std::pair<const uint8_t*, size_t>
OpStream::finish(uint64_t frameSeq, uint32_t w, uint32_t h, bool loadOp,
                 uint32_t clearColorRGBA8) {
    const size_t recBytes = records_.size();
    const size_t total = sizeof(OpHeader) + recBytes + arena_.size();
    out_.resize(total);

    OpHeader hdr{};
    hdr.magic = kMagic;
    hdr.version = 1;
    hdr.flags = loadOp ? 1u : 0u;
    hdr.frameSeq = frameSeq;
    hdr.canvasW = w;
    hdr.canvasH = h;
    hdr.opCount = opCount_;
    hdr.arenaOffset = uint32_t(sizeof(OpHeader) + recBytes);
    hdr.clearColor = clearColorRGBA8;
    std::memcpy(out_.data(), &hdr, sizeof(hdr));
    std::memcpy(out_.data() + sizeof(hdr), records_.data(), recBytes);
    if (!arena_.empty())
        std::memcpy(out_.data() + sizeof(hdr) + recBytes, arena_.data(),
                    arena_.size());
    // Records/arena are NOT consumed: Renderer::renderFrame may pack
    // multiple subsets into one logical frame (atlas-dirty repaint), and
    // each subset's endFrame→finish must include the ops recorded by the
    // earlier subsets. The caller resets the stream at render start
    // (bindings::_vp_render), before prepare-phase uploads record.
    return {out_.data(), out_.size()};
}

} // namespace volcano::web
