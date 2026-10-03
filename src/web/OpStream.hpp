// src/web/OpStream.hpp — op-stream frame serializer (WEBGPU-PLAN §2)
//
// WASM/web builds only (VOLCANO_WEB). A frame is one contiguous region:
// [32 B header][op records][arena]. OpStream::finish() returns {ptr,len}
// into a WASM-stable buffer the JS interpreter reads via typed arrays.
#pragma once

#include <volcano/render/Cmd.hpp>

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace volcano::web {

/// Opcode values — keep in sync with WEBGPU-PLAN.md §2 op table and
/// web/src/opcodes.ts.
enum class Op : uint16_t {
    CreateBuffer  = 1,
    WriteBuffer   = 2,
    ReleaseBuffer = 3,
    CreateTexture = 4,
    WriteTexture  = 5,
    ReleaseTexture= 6,

    DrawTrisPx       = 10,
    DrawTrisPxVC     = 11,
    DrawLineStripPx  = 12,
    DrawSegmentsPx   = 13,
    DrawTextQuads    = 14,
    DrawInstanced    = 15,

    DrawLines        = 20,
    DrawLineSegs     = 21,
    DrawPoints       = 22,
    DrawTrisData     = 23,
    DrawTrisGpu      = 24,
    DrawPie          = 25,
    DrawImage        = 26,
    DrawSurface      = 27,
    DrawGrid3D       = 28,
    DrawSegs3D       = 29,
    DrawTris3D       = 30,
    DrawPoints3D     = 31,
    DrawBoxes3D      = 32,

    TessLines        = 40,
    EvalFunc         = 41,
    FuncDef          = 42,
    ReduceMinMax     = 43,
    KdeEval2D        = 44,
    HistBins         = 45,
    PcmTess          = 46,
    ViolinKde        = 47,
    HistBins2D       = 48,
    HexBins          = 49,
    ContourTess      = 50,
    FftSegments      = 51,
};

/// Reference to bulk data. kind 0 = ARENA (off = byte offset into the
/// region's arena section), 1 = HEAP (off = absolute WASM address).
struct BufSrc {
    uint8_t  kind = 0;
    uint8_t  pad[7] = {};
    uint64_t off = 0;
    uint64_t len = 0;
};
static_assert(sizeof(BufSrc) == 24);

/// Pixel-space rect as f32 (avoids int/float surprises in JS readers).
struct Rect2Df { float x, y, w, h; };

#pragma pack(push, 1)
// 40 B header — see WEBGPU-PLAN.md §2 (the table's field list sums
// to 40; "32 B" in the plan text was a typo).
struct OpHeader {
    uint32_t magic;       // 'VPOP' = 0x564F5050
    uint16_t version;     // 1
    uint16_t flags;       // bit0: loadOp (0=clear,1=load)
    uint64_t frameSeq;
    uint32_t canvasW;
    uint32_t canvasH;
    uint32_t opCount;
    uint32_t arenaOffset; // absolute byte offset of arena within region
    uint32_t clearColor;  // RGBA8 packed
    uint32_t reserved;
};
static_assert(sizeof(OpHeader) == 40);

struct OpRecordHeader {
    uint16_t opcode;
    uint16_t flags;
    uint32_t payloadBytes;
};
static_assert(sizeof(OpRecordHeader) == 8);
#pragma pack(pop)

class OpStream {
public:
    static constexpr uint32_t kMagic = 0x564F5050;

    OpStream() { reset(); }

    /// Clears records/arena (handles never reset). Called by the
    /// embedding at the start of each render cycle — NOT by finish(),
    /// because a repaint (atlas-dirty) packs a second subset whose
    /// frame must still contain the first subset's ops.
    void reset() {
        records_.clear();
        arena_.clear();
        opCount_ = 0;
    }

    // ── Recording ────────────────────────────────────────────────────
    /// Append an op with a raw payload blob.
    void emit(Op op, const void* payload, uint32_t payloadBytes) {
        OpRecordHeader h{uint16_t(op), 0, payloadBytes};
        const size_t off = records_.size();
        records_.resize(off + sizeof(h) + payloadBytes);
        std::memcpy(records_.data() + off, &h, sizeof(h));
        if (payloadBytes)
            std::memcpy(records_.data() + off + sizeof(h), payload,
                        payloadBytes);
        // 4-byte alignment pad
        const size_t pad = (4 - (payloadBytes & 3)) & 3;
        records_.resize(records_.size() + pad, std::byte(0));
        ++opCount_;
    }
    template <class P>
    void emit(Op op, const P& payload) {
        static_assert(std::is_trivially_copyable_v<P>);
        emit(op, &payload, sizeof(P));
    }

    /// Copy span data into the arena; returns a BufSrc referencing it.
    template <class T>
    BufSrc arenaCopy(std::span<const T> data) {
        BufSrc s{};
        s.kind = 0;
        s.off = arena_.size();
        s.len = data.size_bytes();
        const size_t off = arena_.size();
        arena_.resize(off + data.size_bytes());
        std::memcpy(arena_.data() + off, data.data(), data.size_bytes());
        return s;
    }
    /// Arena copy of raw bytes.
    BufSrc arenaCopy(const void* data, size_t bytes) {
        BufSrc s{};
        s.off = arena_.size();
        s.len = bytes;
        const size_t off = arena_.size();
        arena_.resize(off + bytes);
        std::memcpy(arena_.data() + off, data, bytes);
        return s;
    }
    /// Reference caller-stable WASM memory (only for storage that
    /// outlives the frame — series buffers, never temporaries).
    static BufSrc heapRef(const void* ptr, size_t bytes) {
        BufSrc s{};
        s.kind = 1;
        s.off = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(ptr));
        s.len = bytes;
        return s;
    }

    // ── Handles ──────────────────────────────────────────────────────
    /// Monotonic u32 resource ids; never reused within a session.
    uint32_t allocHandle() { return nextHandle_++; }

    // ── Finish ───────────────────────────────────────────────────────
    /// Concatenate header+records+arena into one stable buffer and
    /// return {ptr,len}. The buffer stays valid until the next reset()
    /// (or finish() call) on this OpStream.
    std::pair<const uint8_t*, size_t>
    finish(uint64_t frameSeq, uint32_t w, uint32_t h, bool loadOp,
           uint32_t clearColorRGBA8);

    [[nodiscard]] uint32_t opCount() const { return opCount_; }

private:
    std::vector<std::byte> records_;
    std::vector<std::byte> arena_;
    std::vector<uint8_t> out_;
    uint32_t opCount_ = 0;
    uint32_t nextHandle_ = 1;
};

} // namespace volcano::web
