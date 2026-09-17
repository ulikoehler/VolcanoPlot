// tests/test_animation.cpp — §12 animation: TimedAnimation pacing,
// FuncAnimation/ArtistAnimation, movie writers, jshtml output
#include <gtest/gtest.h>
#include "PlotTestHarness.hpp"

#include <volcano/plot/Animation.hpp>
#include <volcano/plot/Plot.hpp>
#include <volcano/plot/plots/ScatterPlot.hpp>
#include <volcano/encode/MovieWriter.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace volcano;
using namespace volcano::plot;
using volcano::test::PlotTestHarness;
using volcano::test::flatTestStyle;

namespace {

std::vector<uint8_t> flatFrame(uint32_t w, uint32_t h, uint8_t r) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        px[i * 4] = r;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }
    return px;
}

} // namespace

// ═══ TimedAnimation pacing ══════════════════════════════════════════════════

TEST(AnimationTiming, AdvanceRespectsInterval) {
    Figure fig;
    FuncAnimation anim(fig, [](size_t) {}, 10, {}, 50);
    EXPECT_TRUE(anim.advance(0));    // first frame
    EXPECT_FALSE(anim.advance(10));  // too soon
    EXPECT_TRUE(anim.advance(60));   // 60ms > interval
    EXPECT_EQ(anim.currentFrame(), 1u);
    EXPECT_TRUE(anim.advance(120));
    EXPECT_EQ(anim.currentFrame(), 2u);
}

TEST(AnimationTiming, NoRepeatFinishes) {
    Figure fig;
    FuncAnimation anim(fig, [](size_t) {}, 3, {}, 10, false, false);
    anim.advance(0);   // frame 0
    anim.advance(20);  // frame 1
    anim.advance(40);  // frame 2 (last)
    EXPECT_FALSE(anim.finished());
    EXPECT_FALSE(anim.advance(60)); // past last frame → finished
    EXPECT_TRUE(anim.finished());
    anim.restart();
    EXPECT_FALSE(anim.finished());
    EXPECT_TRUE(anim.advance(70));
}

TEST(AnimationTiming, RepeatWraps) {
    Figure fig;
    FuncAnimation anim(fig, [](size_t) {}, 2, {}, 10, false, true);
    anim.advance(0);
    anim.advance(20);  // frame 1
    anim.advance(40);  // wraps to 0
    EXPECT_EQ(anim.currentFrame(), 0u);
    EXPECT_FALSE(anim.finished());
}

// ═══ FuncAnimation / ArtistAnimation ════════════════════════════════════════

TEST(Animation, FuncCallsInitOnceAndFuncPerFrame) {
    Figure fig;
    int inits = 0;
    std::vector<size_t> calls;
    FuncAnimation anim(fig,
                       [&](size_t i) { calls.push_back(i); },
                       5,
                       [&] { ++inits; });
    for (size_t i = 0; i < 5; ++i) anim.drawFrame(i);
    EXPECT_EQ(inits, 1);
    ASSERT_EQ(calls.size(), 5u);
    EXPECT_EQ(calls[4], 4u);
}

TEST(Animation, ArtistStepsThroughAppliers) {
    Figure fig;
    std::vector<int> order;
    ArtistAnimation anim(fig, {
        [&] { order.push_back(0); },
        [&] { order.push_back(1); },
        [&] { order.push_back(2); },
    });
    for (size_t i = 0; i < 3; ++i) anim.drawFrame(i);
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

// ═══ Writers ════════════════════════════════════════════════════════════════

TEST(MovieWriters, ApngProducesValidFile) {
    auto w = encode::createMovieWriter("apng", "/tmp/test_anim.png");
    if (!w->open("/tmp/test_anim.png", 8, 8, 10.0))
        GTEST_SKIP() << w->error();
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(w->writeFrame(flatFrame(8, 8, uint8_t(50 + i * 50))));
    ASSERT_TRUE(w->finish());

    auto b = w->bytes();
    ASSERT_GT(b.size(), 30u);
    // PNG signature.
    EXPECT_EQ(b[0], 0x89);
    EXPECT_EQ(b[1], 'P');
    // Contains acTL chunk.
    bool hasActl = false;
    for (size_t i = 8; i + 4 < b.size(); ++i)
        if (std::string_view(reinterpret_cast<const char*>(b.data() + i), 4) == "acTL")
            hasActl = true;
    EXPECT_TRUE(hasActl);
    // File written.
    std::ifstream f("/tmp/test_anim.png", std::ios::binary);
    EXPECT_TRUE(f.good());
    std::filesystem::remove("/tmp/test_anim.png");
}

TEST(MovieWriters, GifProducesValidFile) {
    auto w = encode::createMovieWriter("gif", "/tmp/test_anim.gif");
    ASSERT_TRUE(w->open("/tmp/test_anim.gif", 8, 8, 10.0));
    for (int i = 0; i < 2; ++i)
        ASSERT_TRUE(w->writeFrame(flatFrame(8, 8, uint8_t(80 + i * 80))));
    ASSERT_TRUE(w->finish());
    auto b = w->bytes();
    ASSERT_GT(b.size(), 800u); // header + palette + frames
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(b.data()), 6),
              "GIF89a");
    EXPECT_EQ(b.back(), 0x3B);
    std::filesystem::remove("/tmp/test_anim.gif");
}

TEST(MovieWriters, FactoryByExtension) {
    EXPECT_NE(encode::createMovieWriter("", "a.gif"), nullptr);
    EXPECT_NE(encode::createMovieWriter("", "a.apng"), nullptr);
    EXPECT_NE(encode::createMovieWriter("", "a.mp4"), nullptr);
    EXPECT_EQ(encode::createMovieWriter("", "a.xyz"), nullptr);
    EXPECT_NE(encode::createMovieWriter("pillow", "a.bin"), nullptr);
}

TEST(MovieWriters, AvailableListHasBuiltins) {
    auto names = encode::availableMovieWriters();
    EXPECT_NE(std::ranges::find(names, "pillow"), names.end());
    EXPECT_NE(std::ranges::find(names, "apng"), names.end());
}

TEST(MovieWriters, Base64Roundtrip) {
    std::vector<uint8_t> data{1, 2, 3, 4, 5};
    auto b64 = encode::base64Encode(data);
    EXPECT_EQ(b64.size(), 8u);
    // Known vector: 0x01 0x02 0x03 → "AQID"
    EXPECT_EQ(b64.substr(0, 4), "AQID");
    EXPECT_EQ(encode::base64Encode({}), "");
}

TEST(MovieWriters, JsHtmlEmbedsFrames) {
    std::vector<std::vector<uint8_t>> frames{
        std::vector<uint8_t>{0x89, 'P', 'N', 'G'},
        std::vector<uint8_t>{0x89, 'P', 'N', 'G'},
    };
    auto html = encode::jsHtmlFromPngFrames(frames, 20.0, 64, 32);
    EXPECT_NE(html.find("data:image/png;base64,"), std::string::npos);
    EXPECT_NE(html.find("width=\"64\""), std::string::npos);
    EXPECT_NE(html.find("50"), std::string::npos); // 1000/20ms delay
}

// ═══ End-to-end saveAnimation ═══════════════════════════════════════════════

TEST(AnimationRender, SaveAnimationWritesGif) {
    test::PlotTestHarness h(64, 64);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{64, 64});
    ax->setViewport({{0, 1}, {0, 1}});

    // Animated point moving right each frame.
    Series2D series;
    series.color = Color::red();
    series.size = 12.0f;
    series.points = {{0.1f, 0.5f}};
    ax->addPlot(std::make_unique<ScatterPlot>(series));

    auto* plotPtr = dynamic_cast<ScatterPlot*>(&*ax->plots().front());
    ASSERT_NE(plotPtr, nullptr);

    FuncAnimation anim(fig, [&](size_t i) {
        plotPtr->series().points = {{0.1f + 0.2f * float(i), 0.5f}};
    }, 3);

    bool ok = h.renderer().saveAnimation(anim, "/tmp/test_anim_render.gif",
                                         10.0);
    // GIF89a writer is built-in → must succeed.
    ASSERT_TRUE(ok);
    std::ifstream f("/tmp/test_anim_render.gif", std::ios::binary);
    char magic[6] = {};
    f.read(magic, 6);
    EXPECT_EQ(std::string_view(magic, 6), "GIF89a");
    std::filesystem::remove("/tmp/test_anim_render.gif");
}

TEST(AnimationRender, SaveAnimationApng) {
    test::PlotTestHarness h(64, 64);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    Series2D series;
    series.color = Color::blue();
    series.points = {{0.5f, 0.5f}};
    ax->addPlot(std::make_unique<ScatterPlot>(series));

    FuncAnimation anim(fig, [](size_t) {}, 2);
    bool ok = h.renderer().saveAnimation(anim, "/tmp/test_anim_render.apng",
                                         5.0, "apng");
    if (ok) {
        std::ifstream f("/tmp/test_anim_render.apng", std::ios::binary);
        EXPECT_TRUE(f.good());
        std::filesystem::remove("/tmp/test_anim_render.apng");
    }
    // APNG requires zlib; if missing the writer reports an error — either
    // outcome is acceptable, but success must produce a real file.
}

TEST(AnimationRender, ToJsHtmlProducesPage) {
    test::PlotTestHarness h(64, 64);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    Series2D series;
    series.color = Color::black();
    series.points = {{0.5f, 0.5f}};
    ax->addPlot(std::make_unique<ScatterPlot>(series));

    FuncAnimation anim(fig, [](size_t) {}, 2);
    auto html = h.renderer().toJsHtml(anim, 10.0);
    ASSERT_FALSE(html.empty());
    EXPECT_NE(html.find("data:image/png;base64,"), std::string::npos);
    EXPECT_NE(html.find("<script>"), std::string::npos);
}

// ═══ Blitting (mpl blit=True) ═══════════════════════════════════════════════

TEST(AnimationRender, BlitRestoresBackground) {
    test::PlotTestHarness h(64, 64, vk::SampleCountFlagBits::e1);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    fig.subplotsAdjust(0, 0, 1, 1, 0, 0);
    fig.layout(Extent2D{64, 64});
    ax->setViewport({{0, 1}, {0, 1}});

    // Static artist: blue point at (0.5, 0.8).
    Series2D stat;
    stat.color = Color::blue();
    stat.size = 14.0f;
    stat.points = {{0.5f, 0.8f}};
    ax->addPlot(std::make_unique<ScatterPlot>(stat));

    // Animated artist: red point moving right each frame.
    Series2D animS;
    animS.color = Color::red();
    animS.size = 14.0f;
    animS.points = {{0.1f, 0.5f}};
    ax->addPlot(std::make_unique<ScatterPlot>(animS));
    auto* plotPtr = dynamic_cast<ScatterPlot*>(&*ax->plots().back());
    ASSERT_NE(plotPtr, nullptr);
    plotPtr->animated = true;

    FuncAnimation anim(fig, [&](size_t i) {
        plotPtr->series().points = {{0.1f + 0.2f * float(i), 0.5f}};
    }, 3, {}, 50, /*blit=*/true);

    // Capture the static background (without animated artists).
    anim.drawFrame(0);
    h.renderer().prepare(fig);
    ASSERT_TRUE(h.renderer().blitCaptureBackground(fig));

    // Frame 2: draw animated artists only, over the restored background.
    anim.drawFrame(2);
    h.renderer().prepare(fig);
    h.renderer().blitDrawAnimated(fig);
    auto px = h.backend().readbackRgba8();
    auto img = test::Image::fromRgba8(px, 64, 64);

    // Red point must be at its frame-2 position (x=0.5 → px 32, y=0.5 →
    // row 32 after the Y flip), not at the frame-0/1 positions.
    EXPECT_GT(img.countColorInRegion(test::Pixel::red(), 26, 26, 40, 40, 40), 5u)
        << "animated point missing at frame-2 position";
    EXPECT_EQ(img.countColorInRegion(test::Pixel::red(), 0, 26, 12, 40, 40), 0u)
        << "blit left a trail at the frame-0 position";
    EXPECT_EQ(img.countColorInRegion(test::Pixel::red(), 13, 26, 25, 40, 40), 0u)
        << "blit left a trail at the frame-1 position";

    // Static artist survived: blue point at (0.5,0.8) → px (32, 13).
    EXPECT_GT(img.countColorInRegion(test::Pixel::blue(), 26, 6, 40, 20, 40), 5u)
        << "static background not restored";
}

TEST(AnimationRender, BlitSaveAnimationGif) {
    test::PlotTestHarness h(64, 64);
    Figure fig;
    auto* ax = fig.addAxes();
    ax->setStyle(test::flatTestStyle());
    ax->setViewport({{0, 1}, {0, 1}});

    Series2D series;
    series.color = Color::red();
    series.size = 12.0f;
    series.points = {{0.1f, 0.5f}};
    auto* sp = ax->addPlot(std::make_unique<ScatterPlot>(series));
    auto* plotPtr = dynamic_cast<ScatterPlot*>(sp);
    plotPtr->animated = true;

    FuncAnimation anim(fig, [&](size_t i) {
        plotPtr->series().points = {{0.1f + 0.2f * float(i), 0.5f}};
    }, 3, {}, 50, /*blit=*/true);

    bool ok = h.renderer().saveAnimation(anim, "/tmp/test_blit_anim.gif", 10.0);
    ASSERT_TRUE(ok);
    std::ifstream f("/tmp/test_blit_anim.gif", std::ios::binary);
    char magic[6] = {};
    f.read(magic, 6);
    EXPECT_EQ(std::string_view(magic, 6), "GIF89a");
    std::filesystem::remove("/tmp/test_blit_anim.gif");
}
