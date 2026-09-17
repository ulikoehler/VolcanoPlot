// tests/test_cjk_rtl.cpp — CJK font fallback + RTL shaping coverage.
#include <gtest/gtest.h>
#include <volcano/plot/Annotation.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Plot.hpp>
#include "PlotTestHarness.hpp"

#include <filesystem>
#include <string>

using namespace volcano;
using namespace volcano::test;
using namespace volcano::plot;

namespace {
struct TFig {
    PlotTestHarness harness;
    Figure figure{1, 1};
    Axes* axes = nullptr;
    explicit TFig(uint32_t size = 256)
        : harness(size, size, vk::SampleCountFlagBits::e1), figure(1, 1) {
        axes = figure.addAxes(0, 0);
        axes->setStyle(flatTestStyle());
        figure.layout(Extent2D{size, size});
    }
    Image render() { return harness.render(figure); }
};

/// True when the system has a broad-coverage fallback font we can find.
bool haveBroadCoverageFont() {
    static const char* names[] = {
        "droidsansfallback", "notosanscjk", "notoserifcjk",
        "wqy", "unifont", "uming", "ukai", "sarasa"};
    for (const char* dir : {"/usr/share/fonts", "/usr/local/share/fonts"}) {
        if (!std::filesystem::exists(dir)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(dir)) {
            std::string n = e.path().filename().string();
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            for (const char* k : names)
                if (n.find(k) != std::string::npos) return true;
        }
    }
    return false;
}

size_t darkPixels(const Image& img) {
    return img.countIf([](uint32_t, uint32_t, Pixel p) {
        return int(p.r) + int(p.g) + int(p.b) < 300;
    });
}
} // namespace

// A CJK label must rasterize through the fallback face (DejaVu lacks
// CJK glyphs; the shared atlas uploads them mid-frame).
TEST(CjkText, RendersViaFallbackFont) {
    if (!haveBroadCoverageFont())
        GTEST_SKIP() << "no CJK-capable fallback font installed";
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.2f, 0.5f, "中文日本語", CoordSystem::Data);
    t->color = Color::black();
    t->fontSize = 2.0f;
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 100u);
}

// Mixed Latin + CJK in one string: run-splitting must keep both faces.
TEST(CjkText, MixedLatinCjkRenders) {
    if (!haveBroadCoverageFont())
        GTEST_SKIP() << "no CJK-capable fallback font installed";
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.1f, 0.5f, "abc中文xyz", CoordSystem::Data);
    t->color = Color::black();
    t->fontSize = 2.0f;
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 150u);
}

// RTL: Hebrew shapes right-to-left via HarfBuzz's direction guessing.
// DejaVu Sans covers Hebrew so no fallback is needed.
TEST(RtlText, HebrewRenders) {
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.2f, 0.5f, "שלום", CoordSystem::Data);
    t->color = Color::black();
    t->fontSize = 2.0f;
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 60u);
}

// Arabic relies on fallback coverage on most systems (DejaVu lacks it);
// the run splitter must route it to the fallback face.
TEST(RtlText, ArabicRenders) {
    if (!haveBroadCoverageFont())
        GTEST_SKIP() << "no broad-coverage fallback font installed";
    TFig cf(256);
    cf.axes->setViewport({0, 1, 0, 1, 0, 1});
    auto* t = cf.axes->text(0.2f, 0.5f, "مرحبا", CoordSystem::Data);
    t->color = Color::black();
    t->fontSize = 2.0f;
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 60u);
}

// measureText must not collapse for RTL/advance-negative runs.
TEST(RtlText, MeasureRtlWidthPositive) {
    TFig cf(128);
    // Indirect: annotation placement uses measureText for alignment —
    // a Hebrew label at right-align must still land on-canvas.
    auto* t = cf.axes->text(0.9f, 0.5f, "שלום", CoordSystem::Axes);
    t->color = Color::black();
    t->fontSize = 1.5f;
    t->halign = HAlign::Right;
    auto img = cf.render();
    EXPECT_GT(darkPixels(img), 30u);
}
