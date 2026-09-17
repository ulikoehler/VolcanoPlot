// tests/test_colormap.cpp — tests for colormap lookup and reversed variants
#include <volcano/plot/Colormap.hpp>

#include <gtest/gtest.h>

#include <limits>
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

// ─── New named colormaps (§3: Crameri + Okabe-Ito) ─────────────────────────

TEST(ColormapNew, BerlinManaguaVanimo) {
    for (const char* name : {"berlin", "managua", "vanimo"}) {
        auto& cm = Colormap::byName(name);
        EXPECT_EQ(cm.name, name);
        EXPECT_EQ(cm.stops.size(), 12u);
        // Reversed variant
        auto& rev = Colormap::byName(std::string(name) + "_r");
        EXPECT_EQ(rev.stops.size(), 12u);
    }
    auto names = Colormap::availableNames();
    std::set<std::string> s(names.begin(), names.end());
    EXPECT_TRUE(s.count("berlin"));
    EXPECT_TRUE(s.count("managua"));
    EXPECT_TRUE(s.count("vanimo"));
}

TEST(ColormapNew, OkabeIto) {
    auto& cm = Colormap::byName("okabe_ito");
    EXPECT_EQ(cm.name, "okabe_ito");
    EXPECT_EQ(cm.stops.size(), 8u);
    // First stop is Okabe-Ito orange #E69F00.
    auto c = cm.sample(0.0f);
    EXPECT_NEAR(c.r, 230.0f / 255.0f, 0.01f);
    EXPECT_NEAR(c.g, 159.0f / 255.0f, 0.01f);
    EXPECT_NEAR(c.b, 0.0f, 0.01f);
}

// ─── ListedColormap (discrete sampling) ────────────────────────────────────

TEST(ColormapListed, DiscreteBins) {
    auto cm = Colormap::listed("rgb3", {
        Color::fromRgba8(255, 0, 0),
        Color::fromRgba8(0, 255, 0),
        Color::fromRgba8(0, 0, 255),
    });
    EXPECT_TRUE(cm.discrete);
    EXPECT_EQ(cm.stops.size(), 3u);

    // Each third of [0,1] maps to exactly one stop, no interpolation.
    auto c0 = cm.sample(0.1f);
    auto c1 = cm.sample(0.5f);
    auto c2 = cm.sample(0.9f);
    EXPECT_NEAR(c0.r, 1.0f, 0.001f); EXPECT_NEAR(c0.g, 0.0f, 0.001f);
    EXPECT_NEAR(c1.g, 1.0f, 0.001f); EXPECT_NEAR(c1.r, 0.0f, 0.001f);
    EXPECT_NEAR(c2.b, 1.0f, 0.001f); EXPECT_NEAR(c2.g, 0.0f, 0.001f);
}

TEST(ColormapListed, DiscreteFalseInterpolates) {
    auto cm = Colormap::listed("bw2", {
        Color::fromRgba8(0, 0, 0),
        Color::fromRgba8(255, 255, 255),
    }, /*discreteSampling=*/false);
    EXPECT_FALSE(cm.discrete);
    auto c = cm.sample(0.5f);
    EXPECT_NEAR(c.r, 0.5f, 0.01f);
}

// ─── LinearSegmentedColormap ───────────────────────────────────────────────

TEST(ColormapSegmented, LinearRamp) {
    // Simple black -> white ramp on all channels.
    auto cm = Colormap::segmented("ramp",
        {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        256);
    EXPECT_EQ(cm.stops.size(), 256u);
    EXPECT_NEAR(cm.sample(0.0f).r, 0.0f, 0.01f);
    EXPECT_NEAR(cm.sample(1.0f).r, 1.0f, 0.01f);
    EXPECT_NEAR(cm.sample(0.5f).r, 0.5f, 0.02f);
}

TEST(ColormapSegmented, PiecewiseChannel) {
    // Red channel: 0 for t<=0.5, ramps 0->1 after (y0=y1=0 at the kink).
    auto cm = Colormap::segmented("step",
        {{0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        256);
    EXPECT_NEAR(cm.sample(0.25f).r, 0.0f, 0.02f);
    EXPECT_NEAR(cm.sample(0.75f).r, 0.5f, 0.05f);
    EXPECT_NEAR(cm.sample(1.0f).r, 1.0f, 0.02f);
}

// ─── bad / under / over special colors ─────────────────────────────────────

TEST(ColormapSpecial, BadForNaN) {
    auto cm = viridis().withBad(Color::fromRgba8(255, 0, 255));
    auto c = cm.sample(std::numeric_limits<float>::quiet_NaN());
    EXPECT_NEAR(c.r, 1.0f, 0.001f);
    EXPECT_NEAR(c.b, 1.0f, 0.001f);
}

TEST(ColormapSpecial, NoBadMeansTransparentNaN) {
    auto c = viridis().sample(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(c.a, 0.0f);
}

TEST(ColormapSpecial, UnderOver) {
    auto cm = viridis()
        .withUnder(Color::fromRgba8(255, 0, 0))
        .withOver(Color::fromRgba8(0, 255, 0));
    auto u = cm.sample(-0.1f);
    auto o = cm.sample(1.1f);
    EXPECT_NEAR(u.r, 1.0f, 0.001f); EXPECT_NEAR(u.g, 0.0f, 0.001f);
    EXPECT_NEAR(o.g, 1.0f, 0.001f); EXPECT_NEAR(o.r, 0.0f, 0.001f);
    // In-range unaffected.
    EXPECT_NEAR(cm.sample(0.0f).r, viridis().stops.front().r, 0.001f);
}

TEST(ColormapSpecial, ReversedSwapsUnderOver) {
    auto cm = viridis()
        .withUnder(Color::fromRgba8(255, 0, 0))
        .withOver(Color::fromRgba8(0, 255, 0))
        .reversed();
    EXPECT_EQ(cm.name, "viridis_r");
    // under/over swap: out-of-range-low now gives the old "over" (green).
    auto u = cm.sample(-0.1f);
    EXPECT_NEAR(u.g, 1.0f, 0.001f);
    auto o = cm.sample(1.1f);
    EXPECT_NEAR(o.r, 1.0f, 0.001f);
}

TEST(ColormapSpecial, ReversedPreservesDiscrete) {
    auto cm = Colormap::listed("l2", {
        Color::fromRgba8(255, 0, 0),
        Color::fromRgba8(0, 0, 255),
    }).reversed();
    EXPECT_TRUE(cm.discrete);
    // t=0.1 -> bin 0 -> last stop (blue, since reversed).
    auto c = cm.sample(0.1f);
    EXPECT_NEAR(c.b, 1.0f, 0.001f);
}
