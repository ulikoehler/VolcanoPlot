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
    reset();   // records/arena consumed; out_ stays valid until next finish
    return {out_.data(), out_.size()};
}

} // namespace volcano::web
