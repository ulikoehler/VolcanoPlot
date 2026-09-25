// tests/test_mplcanvas.cpp — regression tests for the mpl-backend
// Vulkan canvas (ring closing conventions, fill, scissor, readback).
#include <gtest/gtest.h>

#include <volcano/render/MplCanvas.hpp>

using namespace volcano;

namespace {

std::vector<uint8_t> renderQuad(std::span<const float> verts,
                                std::span<const uint8_t> codes,
                                uint32_t w = 64, uint32_t h = 64,
                                std::optional<plot::Rect2D> clip =
                                    std::nullopt) {
    auto cv = render::MplCanvas::headless(w, h,
                                          vk::SampleCountFlagBits::e1);
    cv->beginFrame();
    cv->clear({1, 1, 1, 1});
    std::vector<float> empty;
    std::vector<plot::Point2D> noRing;
    cv->path(verts, codes, {1, 0, 0, 1}, true, {0, 0, 0, 0}, 0.0f, 0.0f,
             empty, plot::JoinStyle::Miter, plot::CapStyle::Butt, "",
             {0, 0, 0, 0}, clip, noRing);
    cv->endFrame();
    return cv->readback();
}

uint32_t countRed(const std::vector<uint8_t>& px) {
    uint32_t n = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i] > 200 && px[i + 1] < 60 && px[i + 2] < 60) ++n;
    return n;
}

} // namespace

TEST(MplCanvas, ClosePolyCodeWithoutVertexFills) {
    // mpl QuadMesh patches can carry a trailing CLOSEPOLY code with
    // no matching vertex — the canvas must synthesize the closing
    // point so the ring fills.
    std::vector<float> verts = {8, 8, 40, 8, 40, 40, 8, 40};
    std::vector<uint8_t> codes = {1, 2, 2, 2, 79};
    auto px = renderQuad(verts, codes);
    EXPECT_GT(countRed(px), 700u);
}

TEST(MplCanvas, LinetoCoincidentCloseFills) {
    // mpl also closes paths by repeating the first vertex as LINETO
    // (no CLOSEPOLY code) — treat that ring as closed too.
    std::vector<float> verts = {8, 8, 40, 8, 40, 40, 8, 40, 8, 8};
    std::vector<uint8_t> codes = {1, 2, 2, 2, 2};
    auto px = renderQuad(verts, codes);
    EXPECT_GT(countRed(px), 700u);
}

TEST(MplCanvas, OpenPathDoesNotFill) {
    std::vector<float> verts = {8, 8, 40, 8, 40, 40, 8, 40};
    std::vector<uint8_t> codes = {1, 2, 2, 2};
    auto px = renderQuad(verts, codes);
    EXPECT_EQ(countRed(px), 0u);
}

TEST(MplCanvas, ScissorClipsFill) {
    std::vector<float> verts = {8, 8, 56, 8, 56, 56, 8, 56};
    std::vector<uint8_t> codes = {1, 2, 2, 2, 79};
    auto px = renderQuad(verts, codes, 64, 64,
                         plot::Rect2D{0, 0, 24, 64});
    EXPECT_GT(countRed(px), 0u);
    // Nothing may leak right of the 24px scissor.
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 24; x < 64; ++x) {
            size_t i = (size_t(y) * 64 + x) * 4;
            EXPECT_FALSE(px[i] > 200 && px[i + 1] < 60 &&
                         px[i + 2] < 60);
        }
}

TEST(MplCanvas, ClearColorApplied) {
    auto cv = render::MplCanvas::headless(32, 32,
                                          vk::SampleCountFlagBits::e1);
    cv->beginFrame();
    cv->clear({0, 0, 1, 1});
    cv->endFrame();
    auto px = cv->readback();
    ASSERT_EQ(px.size(), 32u * 32u * 4u);
    EXPECT_GT(px[2], 200);
    EXPECT_LT(px[0], 60);
}
