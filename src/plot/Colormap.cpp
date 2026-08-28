// volcano/plot/Colormap.cpp
#include "volcano/plot/Colormap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volcano::plot {

Color Colormap::sample(float t) const {
    if (stops.empty()) return Color::black();
    t = std::clamp(t, 0.0f, 1.0f);
    if (stops.size() == 1) return stops.front();
    float scaled = t * (stops.size() - 1);
    size_t i = static_cast<size_t>(scaled);
    if (i >= stops.size() - 1) return stops.back();
    float f = scaled - i;
    const auto& a = stops[i];
    const auto& b = stops[i + 1];
    return { a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f,
             a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f };
}

namespace colormaps {

// Viridis (matplotlib default) — 16 stops sampled.
const Colormap& viridis() {
    static const Colormap cm = { "viridis", {
        Color::fromRgba8(68,1,84),   Color::fromRgba8(72,40,120),
        Color::fromRgba8(62,74,137), Color::fromRgba8(49,104,142),
        Color::fromRgba8(38,130,142),Color::fromRgba8(31,158,137),
        Color::fromRgba8(53,183,121),Color::fromRgba8(110,206,88),
        Color::fromRgba8(181,222,43),Color::fromRgba8(253,231,37),
    }};
    return cm;
}

const Colormap& plasma() {
    static const Colormap cm = { "plasma", {
        Color::fromRgba8(13,8,135),  Color::fromRgba8(75,3,161),
        Color::fromRgba8(125,3,168), Color::fromRgba8(168,34,150),
        Color::fromRgba8(203,70,121),Color::fromRgba8(229,107,93),
        Color::fromRgba8(248,148,65),Color::fromRgba8(253,195,40),
        Color::fromRgba8(240,249,33),
    }};
    return cm;
}

const Colormap& inferno() {
    static const Colormap cm = { "inferno", {
        Color::fromRgba8(0,0,4),     Color::fromRgba8(40,11,84),
        Color::fromRgba8(101,21,110),Color::fromRgba8(159,42,99),
        Color::fromRgba8(212,72,66), Color::fromRgba8(245,125,21),
        Color::fromRgba8(250,193,39),Color::fromRgba8(252,255,164),
    }};
    return cm;
}

const Colormap& magma() {
    static const Colormap cm = { "magma", {
        Color::fromRgba8(0,0,4),     Color::fromRgba8(28,16,68),
        Color::fromRgba8(79,18,90),  Color::fromRgba8(129,37,79),
        Color::fromRgba8(181,54,96), Color::fromRgba8(229,80,77),
        Color::fromRgba8(251,135,6), Color::fromRgba8(252,253,191),
    }};
    return cm;
}

const Colormap& cividis() {
    static const Colormap cm = { "cividis", {
        Color::fromRgba8(0,32,76),   Color::fromRgba8(53,50,123),
        Color::fromRgba8(97,72,159), Color::fromRgba8(141,97,184),
        Color::fromRgba8(181,124,201),Color::fromRgba8(217,162,213),
        Color::fromRgba8(237,202,219),Color::fromRgba8(251,237,223),
    }};
    return cm;
}

const Colormap& turbo() {
    static const Colormap cm = { "turbo", {
        Color::fromRgba8(48,18,59),  Color::fromRgba8(70,107,227),
        Color::fromRgba8(33,168,233),Color::fromRgba8(29,224,163),
        Color::fromRgba8(98,255,95), Color::fromRgba8(197,255,51),
        Color::fromRgba8(255,210,30),Color::fromRgba8(255,128,0),
        Color::fromRgba8(255,32,32), Color::fromRgba8(124,12,72),
    }};
    return cm;
}

const Colormap& jet() {
    static const Colormap cm = { "jet", {
        Color::fromRgba8(0,0,131),   Color::fromRgba8(0,60,170),
        Color::fromRgba8(0,170,255), Color::fromRgba8(0,255,170),
        Color::fromRgba8(170,255,0), Color::fromRgba8(255,170,0),
        Color::fromRgba8(255,60,0),  Color::fromRgba8(131,0,0),
    }};
    return cm;
}

const Colormap& coolwarm() {
    static const Colormap cm = { "coolwarm", {
        Color::fromRgba8(59,76,192), Color::fromRgba8(98,130,234),
        Color::fromRgba8(141,176,254),Color::fromRgba8(184,208,255),
        Color::fromRgba8(221,221,221),Color::fromRgba8(245,196,207),
        Color::fromRgba8(245,106,145),Color::fromRgba8(221,50,75),
        Color::fromRgba8(180,4,38),
    }};
    return cm;
}

const Colormap& RdBu() {
    static const Colormap cm = { "RdBu", {
        Color::fromRgba8(103,0,31),  Color::fromRgba8(178,27,42),
        Color::fromRgba8(214,96,77), Color::fromRgba8(244,165,130),
        Color::fromRgba8(253,219,199),Color::fromRgba8(224,243,248),
        Color::fromRgba8(146,197,222),Color::fromRgba8(67,147,195),
        Color::fromRgba8(33,102,172),
    }};
    return cm;
}

const Colormap& grayscale() {
    static const Colormap cm = { "grayscale", {
        Color::fromRgba8(0,0,0),     Color::fromRgba8(64,64,64),
        Color::fromRgba8(128,128,128),Color::fromRgba8(192,192,192),
        Color::fromRgba8(255,255,255),
    }};
    return cm;
}

const Colormap& seismic() {
    static const Colormap cm = { "seismic", {
        Color::fromRgba8(0,0,135),   Color::fromRgba8(33,65,198),
        Color::fromRgba8(107,174,214),Color::fromRgba8(198,219,239),
        Color::fromRgba8(245,245,245),Color::fromRgba8(239,154,97),
        Color::fromRgba8(214,85,33), Color::fromRgba8(135,0,0),
    }};
    return cm;
}

} // namespace colormaps

const Colormap& Colormap::byName(std::string_view name) {
    if (name == "viridis")  return colormaps::viridis();
    if (name == "plasma")   return colormaps::plasma();
    if (name == "inferno")  return colormaps::inferno();
    if (name == "magma")    return colormaps::magma();
    if (name == "cividis")  return colormaps::cividis();
    if (name == "turbo")    return colormaps::turbo();
    if (name == "jet")      return colormaps::jet();
    if (name == "coolwarm") return colormaps::coolwarm();
    if (name == "RdBu")     return colormaps::RdBu();
    if (name == "seismic")  return colormaps::seismic();
    return colormaps::grayscale();
}

std::vector<std::string> Colormap::availableNames() {
    return { "viridis","plasma","inferno","magma","cividis","turbo","jet",
             "coolwarm","RdBu","seismic","grayscale" };
}

} // namespace volcano::plot
