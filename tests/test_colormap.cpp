// tests/test_colormap.cpp — tests for colormap lookup and reversed variants
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace volcano::plot;
using namespace volcano::plot::colormaps;

// ─── Basic sampling ────────────────────────────────────────────────────────

TEST(Colormap, SampleAt0ReturnsFirstStop) {
    auto c = viridis().sample(0.0f);
    EXPECT_NEAR(c.r, 68.0f / 255.0f, 0.01f);
}

TEST(Colormap, SampleAt1ReturnsLastStop) {
    auto c = viridis().sample(1.0f);
    EXPECT_NEAR(c.r, 253.0f / 255.0f, 0.01f);
}

TEST(Colormap, SampleAt0_5IsMidpoint) {
    auto c = viridis().sample(0.5f);
    // Should be between stop 7 and 8 (of 10 stops, index 4.5)
    auto a = viridis().stops[4];
    auto b = viridis().stops[5];
    EXPECT_NEAR(c.r, (a.r + b.r) / 2.0f, 0.01f);
}

TEST(Colormap, SampleClampsOutOfRange) {
    auto below = viridis().sample(-0.5f);
    auto above = viridis().sample(1.5f);
    EXPECT_NEAR(below.r, viridis().stops.front().r, 0.001f);
    EXPECT_NEAR(above.r, viridis().stops.back().r, 0.001f);
}

// ─── byName lookup ─────────────────────────────────────────────────────────

TEST(ColormapByName, Viridis) {
    auto& cm = Colormap::byName("viridis");
    EXPECT_EQ(cm.name, "viridis");
}

TEST(ColormapByName, UnknownReturnsGrayscale) {
    auto& cm = Colormap::byName("nonexistent");
    EXPECT_EQ(cm.name, "grayscale");
}

TEST(ColormapByName, SequentialBlues) {
    auto& cm = Colormap::byName("Blues");
    EXPECT_EQ(cm.name, "Blues");
    EXPECT_FALSE(cm.stops.empty());
}

TEST(ColormapByName, DivergingPiYG) {
    auto& cm = Colormap::byName("PiYG");
    EXPECT_EQ(cm.name, "PiYG");
}

TEST(ColormapByName, CyclicTwilight) {
    auto& cm = Colormap::byName("twilight");
    EXPECT_EQ(cm.name, "twilight");
}

TEST(ColormapByName, QualitativeTab10) {
    auto& cm = Colormap::byName("tab10");
    EXPECT_EQ(cm.name, "tab10");
}

TEST(ColormapByName, MiscTerrain) {
    auto& cm = Colormap::byName("terrain");
    EXPECT_EQ(cm.name, "terrain");
}

// ─── Reversed colormaps ────────────────────────────────────────────────────

TEST(ColormapReversed, ViridisRIsReversed) {
    auto& fwd = Colormap::byName("viridis");
    auto& rev = Colormap::byName("viridis_r");
    EXPECT_EQ(rev.name, "viridis_r");
    ASSERT_EQ(fwd.stops.size(), rev.stops.size());
    for (size_t i = 0; i < fwd.stops.size(); ++i) {
        EXPECT_NEAR(fwd.stops[i].r, rev.stops[fwd.stops.size() - 1 - i].r, 0.001f);
        EXPECT_NEAR(fwd.stops[i].g, rev.stops[fwd.stops.size() - 1 - i].g, 0.001f);
        EXPECT_NEAR(fwd.stops[i].b, rev.stops[fwd.stops.size() - 1 - i].b, 0.001f);
    }
}

TEST(ColormapReversed, SampleReversedAt0IsLastStop) {
    auto& rev = Colormap::byName("Blues_r");
    auto& fwd = Colormap::byName("Blues");
    auto c = rev.sample(0.0f);
    EXPECT_NEAR(c.r, fwd.stops.back().r, 0.01f);
}

TEST(ColormapReversed, SampleReversedAt1IsFirstStop) {
    auto& rev = Colormap::byName("Blues_r");
    auto& fwd = Colormap::byName("Blues");
    auto c = rev.sample(1.0f);
    EXPECT_NEAR(c.r, fwd.stops.front().r, 0.01f);
}

TEST(ColormapReversed, CachedOnRepeatLookup) {
    auto& rev1 = Colormap::byName("hot_r");
    auto& rev2 = Colormap::byName("hot_r");
    EXPECT_EQ(&rev1, &rev2); // Same reference (cached)
}

TEST(ColormapReversed, SampleReversedMethod) {
    auto& cm = Colormap::byName("viridis");
    auto at0 = cm.sample(0.0f);
    auto revAt0 = cm.sampleReversed(0.0f);
    // sampleReversed(0) = sample(1) = last stop
    EXPECT_NEAR(revAt0.r, cm.stops.back().r, 0.001f);
    EXPECT_NEAR(revAt0.r, at0.r == cm.stops.back().r ? at0.r : cm.stops.back().r, 0.01f);
}

// ─── availableNames ────────────────────────────────────────────────────────

TEST(ColormapAvailable, ContainsAllCategories) {
    auto names = Colormap::availableNames();
    std::set<std::string> nameSet(names.begin(), names.end());

    // Perceptually uniform
    EXPECT_TRUE(nameSet.count("viridis"));
    EXPECT_TRUE(nameSet.count("plasma"));

    // Sequential
    EXPECT_TRUE(nameSet.count("Blues"));
    EXPECT_TRUE(nameSet.count("Greens"));
    EXPECT_TRUE(nameSet.count("hot"));
    EXPECT_TRUE(nameSet.count("copper"));

    // Diverging
    EXPECT_TRUE(nameSet.count("PiYG"));
    EXPECT_TRUE(nameSet.count("Spectral"));
    EXPECT_TRUE(nameSet.count("bwr"));

    // Cyclic
    EXPECT_TRUE(nameSet.count("twilight"));
    EXPECT_TRUE(nameSet.count("hsv"));

    // Qualitative
    EXPECT_TRUE(nameSet.count("tab10"));
    EXPECT_TRUE(nameSet.count("Set1"));
    EXPECT_TRUE(nameSet.count("Paired"));

    // Miscellaneous
    EXPECT_TRUE(nameSet.count("terrain"));
    EXPECT_TRUE(nameSet.count("rainbow"));
    EXPECT_TRUE(nameSet.count("cubehelix"));
}

TEST(ColormapAvailable, HasAtLeast60Names) {
    auto names = Colormap::availableNames();
    // 11 original + ~60 new = 70+
    EXPECT_GE(names.size(), 60u);
}

TEST(ColormapAvailable, NoDuplicateNames) {
    auto names = Colormap::availableNames();
    std::set<std::string> nameSet(names.begin(), names.end());
    EXPECT_EQ(names.size(), nameSet.size());
}

// ─── All colormaps are valid ───────────────────────────────────────────────

TEST(ColormapAllValid, AllHaveStops) {
    auto names = Colormap::availableNames();
    for (const auto& name : names) {
        auto& cm = Colormap::byName(name);
        EXPECT_FALSE(cm.stops.empty()) << "Colormap '" << name << "' has no stops";
        EXPECT_EQ(cm.name, name) << "Colormap name mismatch for '" << name << "'";
    }
}

TEST(ColormapAllValid, AllReversedHaveStops) {
    auto names = Colormap::availableNames();
    for (const auto& name : names) {
        std::string revName = name + "_r";
        auto& cm = Colormap::byName(revName);
        EXPECT_FALSE(cm.stops.empty()) << "Reversed colormap '" << revName << "' has no stops";
        EXPECT_EQ(cm.name, revName);
    }
}
