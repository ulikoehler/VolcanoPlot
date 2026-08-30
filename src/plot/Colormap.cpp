// volcano/plot/Colormap.cpp
#include "volcano/plot/Colormap.hpp"

#include <algorithm>
#include <cmath>
#include <map>
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

namespace {

/// Lookup table mapping colormap names to their factory functions.
struct NameEntry {
    const char* name;
    const Colormap& (*fn)();
};

const std::vector<NameEntry>& colormapTable() {
    static const std::vector<NameEntry> table = {
        // Perceptually uniform + misc (already existed)
        {"viridis",  colormaps::viridis},
        {"plasma",   colormaps::plasma},
        {"inferno",  colormaps::inferno},
        {"magma",    colormaps::magma},
        {"cividis",  colormaps::cividis},
        {"turbo",    colormaps::turbo},
        {"jet",      colormaps::jet},
        {"coolwarm", colormaps::coolwarm},
        {"RdBu",     colormaps::RdBu},
        {"seismic",  colormaps::seismic},
        {"grayscale",colormaps::grayscale},
        // Sequential (§3.2)
        {"Greys",    colormaps::Greys},
        {"Purples",  colormaps::Purples},
        {"Blues",    colormaps::Blues},
        {"Greens",   colormaps::Greens},
        {"Oranges",  colormaps::Oranges},
        {"Reds",     colormaps::Reds},
        {"YlOrBr",   colormaps::YlOrBr},
        {"YlOrRd",   colormaps::YlOrRd},
        {"OrRd",     colormaps::OrRd},
        {"PuRd",     colormaps::PuRd},
        {"RdPu",     colormaps::RdPu},
        {"BuPu",     colormaps::BuPu},
        {"GnBu",     colormaps::GnBu},
        {"PuBu",     colormaps::PuBu},
        {"YlGnBu",   colormaps::YlGnBu},
        {"PuBuGn",   colormaps::PuBuGn},
        {"BuGn",     colormaps::BuGn},
        {"YlGn",     colormaps::YlGn},
        {"gray",     colormaps::gray},
        {"bone",     colormaps::bone},
        {"pink",     colormaps::pink},
        {"spring",   colormaps::spring},
        {"summer",   colormaps::summer},
        {"autumn",   colormaps::autumn},
        {"winter",   colormaps::winter},
        {"cool",     colormaps::cool},
        {"Wistia",   colormaps::Wistia},
        {"hot",      colormaps::hot},
        {"afmhot",   colormaps::afmhot},
        {"gist_heat",colormaps::gist_heat},
        {"copper",   colormaps::copper},
        // Diverging (§3.3)
        {"PiYG",     colormaps::PiYG},
        {"PRGn",     colormaps::PRGn},
        {"BrBG",     colormaps::BrBG},
        {"PuOr",     colormaps::PuOr},
        {"RdGy",     colormaps::RdGy},
        {"RdYlBu",   colormaps::RdYlBu},
        {"RdYlGn",   colormaps::RdYlGn},
        {"Spectral", colormaps::Spectral},
        {"bwr",      colormaps::bwr},
        // Cyclic (§3.4)
        {"twilight",         colormaps::twilight},
        {"twilight_shifted", colormaps::twilight_shifted},
        {"hsv",              colormaps::hsv},
        // Qualitative (§3.5)
        {"Pastel1",  colormaps::Pastel1},
        {"Pastel2",  colormaps::Pastel2},
        {"Paired",   colormaps::Paired},
        {"Accent",   colormaps::Accent},
        {"Dark2",    colormaps::Dark2},
        {"Set1",     colormaps::Set1},
        {"Set2",     colormaps::Set2},
        {"Set3",     colormaps::Set3},
        {"tab10",    colormaps::tab10},
        {"tab20",    colormaps::tab20},
        {"tab20b",   colormaps::tab20b},
        {"tab20c",   colormaps::tab20c},
        // Miscellaneous (§3.6)
        {"flag",          colormaps::flag},
        {"prism",         colormaps::prism},
        {"ocean",         colormaps::ocean},
        {"gist_earth",    colormaps::gist_earth},
        {"terrain",       colormaps::terrain},
        {"gist_stern",    colormaps::gist_stern},
        {"gnuplot",       colormaps::gnuplot},
        {"gnuplot2",      colormaps::gnuplot2},
        {"CMRmap",        colormaps::CMRmap},
        {"cubehelix",     colormaps::cubehelix},
        {"brg",           colormaps::brg},
        {"gist_rainbow",  colormaps::gist_rainbow},
        {"rainbow",       colormaps::rainbow},
        {"nipy_spectral", colormaps::nipy_spectral},
        {"gist_ncar",     colormaps::gist_ncar},
    };
    return table;
}

/// Cache of reversed colormaps, created on first lookup.
/// Key: colormap name (without "_r" suffix).
/// Returns a reference to the cached reversed colormap.
const Colormap& getReversed(std::string_view baseName) {
    // Find the base colormap.
    const auto& table = colormapTable();
    const Colormap* base = nullptr;
    for (const auto& entry : table) {
        if (entry.name == baseName) {
            base = &entry.fn();
            break;
        }
    }
    if (!base) return colormaps::grayscale();

    // Use a static map keyed by name to cache reversed colormaps.
    // This is thread-safe in C++11+ for static local initialization,
    // but the map itself needs protection. Since VolcanoPlot is
    // single-threaded for rendering, this is fine.
    static std::map<std::string, Colormap, std::less<>> cache;
    std::string key(baseName);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    // Create the reversed colormap.
    Colormap rev;
    rev.name = std::string(baseName) + "_r";
    rev.stops.reserve(base->stops.size());
    for (auto it2 = base->stops.rbegin(); it2 != base->stops.rend(); ++it2)
        rev.stops.push_back(*it2);
    auto [inserted, _] = cache.emplace(std::move(key), std::move(rev));
    return inserted->second;
}

} // namespace

const Colormap& Colormap::byName(std::string_view name) {
    // Check for reversed variant: name ends with "_r"
    if (name.size() > 2 && name.substr(name.size() - 2) == "_r") {
        return getReversed(name.substr(0, name.size() - 2));
    }

    const auto& table = colormapTable();
    for (const auto& entry : table) {
        if (entry.name == name) return entry.fn();
    }
    return colormaps::grayscale();
}

std::vector<std::string> Colormap::availableNames() {
    const auto& table = colormapTable();
    std::vector<std::string> names;
    names.reserve(table.size());
    for (const auto& entry : table)
        names.emplace_back(entry.name);
    return names;
}

} // namespace volcano::plot
