// tests/test_opstream.cpp — op-stream wire format (native test; the
// stream itself is platform-agnostic, WASM-side emission and JS-side
// decoding share this byte layout — see WEBGPU-PLAN.md §2).
#include "../src/web/OpStream.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace volcano::web;

namespace {

struct Reader {
    const uint8_t* base; size_t len;
    uint32_t u32(size_t o) const {
        uint32_t v; std::memcpy(&v, base + o, 4); return v; }
    uint16_t u16(size_t o) const {
        uint16_t v; std::memcpy(&v, base + o, 2); return v; }
    uint64_t u64(size_t o) const {
        uint64_t v; std::memcpy(&v, base + o, 8); return v; }
};

TEST(OpStream, HeaderLayout) {
    OpStream s;
    auto [ptr, len] = s.finish(/*seq*/1, /*w*/1920, /*h*/1080,
                               /*load*/false, /*clear*/0x11223344u);
    ASSERT_GE(len, sizeof(OpHeader));
    Reader r{ptr, len};
    EXPECT_EQ(r.u32(0), 0x564F5050u);            // 'VPOP'
    EXPECT_EQ(r.u16(4), 1u);                     // version
    EXPECT_EQ(r.u16(6) & 1, 0u);                 // flags bit0 = loadOp
    EXPECT_EQ(r.u64(8), 1u);                     // frameSeq
    EXPECT_EQ(r.u32(16), 1920u);
    EXPECT_EQ(r.u32(20), 1080u);
    EXPECT_EQ(r.u32(24), 0u);                    // opCount
    EXPECT_EQ(r.u32(28), sizeof(OpHeader));      // arenaOffset
    EXPECT_EQ(r.u32(32), 0x11223344u);           // clearColor
}

TEST(OpStream, LoadFlagSetOnLoadFrames) {
    OpStream s;
    auto [ptr, len] = s.finish(1, 8, 8, true, 0);
    EXPECT_EQ((Reader{ptr, len}).u16(6) & 1, 1u);
}

TEST(OpStream, OpRecordAlignedAndPacked) {
    OpStream s;
    struct P { uint32_t a; uint16_t b; };        // 8 B (padded)
    s.emit(Op::DrawLines, P{0xdeadbeef, 0x00aa});
    s.emit(Op::ReleaseBuffer, uint32_t{7});
    auto [ptr, len] = s.finish(1, 64, 64, false, 0);
    Reader r{ptr, len};
    const size_t H = sizeof(OpHeader);           // 40
    EXPECT_EQ(r.u32(24), 2u);                    // opCount
    EXPECT_EQ(r.u16(H + 0), uint16_t(Op::DrawLines));
    EXPECT_EQ(r.u32(H + 4), 8u);                 // payloadBytes
    EXPECT_EQ(r.u32(H + 8), 0xdeadbeefu);
    // rec1 starts at H + 8 + align4(8) = H + 16
    EXPECT_EQ(r.u16(H + 16), uint16_t(Op::ReleaseBuffer));
    EXPECT_EQ(r.u32(H + 24), 7u);
    EXPECT_EQ(r.u32(28), H + 28);                // arenaOffset
}

TEST(OpStream, ArenaCopiesBytes) {
    OpStream s;
    float vals[3] = {1.5f, -2.f, 3.25f};
    BufSrc src = s.arenaCopy(vals, sizeof vals);
    EXPECT_EQ(src.kind, 0u);
    EXPECT_EQ(src.len, sizeof vals);
    auto [ptr, len] = s.finish(1, 1, 1, false, 0);
    Reader r{ptr, len};
    const size_t arena = r.u32(28);
    float out[3]; std::memcpy(out, ptr + arena + src.off, sizeof out);
    EXPECT_FLOAT_EQ(out[0], 1.5f);
    EXPECT_FLOAT_EQ(out[1], -2.f);
    EXPECT_FLOAT_EQ(out[2], 3.25f);
}

TEST(OpStream, HandlesMonotonicAcrossFrames) {
    OpStream s;
    uint32_t h0 = s.allocHandle(), h1 = s.allocHandle();
    s.finish(1, 1, 1, false, 0);
    uint32_t h2 = s.allocHandle();
    EXPECT_EQ(h0 + 1, h1);
    EXPECT_EQ(h1 + 1, h2);                       // not reset per frame
}

TEST(OpStream, ResourceOpsRecordedBetweenFramesArePackedNextFinish) {
    // Uploads during Renderer::prepare() land on the stream *before*
    // beginFrame — finish() must still see them (record order ==
    // execution order). Regression test for the beginFrame-resets-stream
    // bug.
    OpStream s;
    s.emit(Op::CreateBuffer, uint32_t{42});      // "prepare-phase" op
    s.finish(1, 1, 1, false, 0);                 // frame 1 packed it
    s.emit(Op::WriteBuffer, uint32_t{42});       // between frames
    auto [ptr, len] = s.finish(2, 1, 1, false, 0);
    Reader r{ptr, len};
    EXPECT_EQ(r.u32(24), 1u);
    EXPECT_EQ(r.u16(sizeof(OpHeader)), uint16_t(Op::WriteBuffer));
}

TEST(OpStream, FinishClearsRecordsButNotHandles) {
    OpStream s;
    s.emit(Op::CreateBuffer, uint32_t{1});
    s.finish(1, 1, 1, false, 0);
    auto [ptr, len] = s.finish(2, 1, 1, false, 0);
    EXPECT_EQ((Reader{ptr, len}).u32(24), 0u);
    EXPECT_EQ((Reader{ptr, len}).u32(28), len);    // empty arena
}

} // namespace
