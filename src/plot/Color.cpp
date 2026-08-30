// volcano/plot/Color.cpp — color string parsing and default color cycle
#include "volcano/plot/Types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace volcano::plot {

namespace {

// ─── Default color cycle (matplotlib "tab10") ──────────────────────────────

constexpr std::array<Color, 10> kTab10 = {{
    Color::fromRgba8(31,  119, 180), // C0  blue
    Color::fromRgba8(255, 127, 14),  // C1  orange
    Color::fromRgba8(44,  160, 44),  // C2  green
    Color::fromRgba8(214, 39,  40),  // C3  red
    Color::fromRgba8(148, 103, 189), // C4  purple
    Color::fromRgba8(140, 86,  75),  // C5  brown
    Color::fromRgba8(227, 119, 194), // C6  pink
    Color::fromRgba8(127, 127, 127), // C7  gray
    Color::fromRgba8(188, 189, 34),  // C8  olive
    Color::fromRgba8(23,  190, 207), // C9  cyan
}};

// ─── CSS4 / X11 named colors ────────────────────────────────────────────────

const std::map<std::string, Color, std::less<>>& namedColors() {
    static const std::map<std::string, Color, std::less<>> table = {
        {"aliceblue",            Color::fromRgba8(240, 248, 255)},
        {"antiquewhite",         Color::fromRgba8(250, 235, 215)},
        {"aqua",                 Color::fromRgba8(  0, 255, 255)},
        {"aquamarine",           Color::fromRgba8(127, 255, 212)},
        {"azure",                Color::fromRgba8(240, 255, 255)},
        {"beige",                Color::fromRgba8(245, 245, 220)},
        {"bisque",               Color::fromRgba8(255, 228, 196)},
        {"black",                Color::fromRgba8(  0,   0,   0)},
        {"blanchedalmond",       Color::fromRgba8(255, 235, 205)},
        {"blue",                 Color::fromRgba8(  0,   0, 255)},
        {"blueviolet",           Color::fromRgba8(138,  43, 226)},
        {"brown",                Color::fromRgba8(165,  42,  42)},
        {"burlywood",            Color::fromRgba8(222, 184, 135)},
        {"cadetblue",            Color::fromRgba8( 95, 158, 160)},
        {"chartreuse",           Color::fromRgba8(127, 255,   0)},
        {"chocolate",            Color::fromRgba8(210, 105,  30)},
        {"coral",                Color::fromRgba8(255, 127,  80)},
        {"cornflowerblue",       Color::fromRgba8(100, 149, 237)},
        {"cornsilk",             Color::fromRgba8(255, 248, 220)},
        {"crimson",              Color::fromRgba8(220,  20,  60)},
        {"cyan",                 Color::fromRgba8(  0, 255, 255)},
        {"darkblue",             Color::fromRgba8(  0,   0, 139)},
        {"darkcyan",             Color::fromRgba8(  0, 139, 139)},
        {"darkgoldenrod",        Color::fromRgba8(184, 134,  11)},
        {"darkgray",             Color::fromRgba8(169, 169, 169)},
        {"darkgreen",            Color::fromRgba8(  0, 100,   0)},
        {"darkgrey",             Color::fromRgba8(169, 169, 169)},
        {"darkkhaki",            Color::fromRgba8(189, 183, 107)},
        {"darkmagenta",          Color::fromRgba8(139,   0, 139)},
        {"darkolivegreen",       Color::fromRgba8( 85, 107,  47)},
        {"darkorange",           Color::fromRgba8(255, 140,   0)},
        {"darkorchid",           Color::fromRgba8(153,  50, 204)},
        {"darkred",              Color::fromRgba8(139,   0,   0)},
        {"darksalmon",           Color::fromRgba8(233, 150, 122)},
        {"darkseagreen",         Color::fromRgba8(143, 188, 143)},
        {"darkslateblue",        Color::fromRgba8( 72,  61, 139)},
        {"darkslategray",        Color::fromRgba8( 47,  79,  79)},
        {"darkslategrey",        Color::fromRgba8( 47,  79,  79)},
        {"darkturquoise",        Color::fromRgba8(  0, 206, 209)},
        {"darkviolet",           Color::fromRgba8(148,   0, 211)},
        {"deeppink",             Color::fromRgba8(255,  20, 147)},
        {"deepskyblue",          Color::fromRgba8(  0, 191, 255)},
        {"dimgray",              Color::fromRgba8(105, 105, 105)},
        {"dimgrey",              Color::fromRgba8(105, 105, 105)},
        {"dodgerblue",           Color::fromRgba8( 30, 144, 255)},
        {"firebrick",            Color::fromRgba8(178,  34,  34)},
        {"floralwhite",          Color::fromRgba8(255, 250, 240)},
        {"forestgreen",          Color::fromRgba8( 34, 139,  34)},
        {"fuchsia",              Color::fromRgba8(255,   0, 255)},
        {"gainsboro",            Color::fromRgba8(220, 220, 220)},
        {"ghostwhite",           Color::fromRgba8(248, 248, 255)},
        {"gold",                 Color::fromRgba8(255, 215,   0)},
        {"goldenrod",            Color::fromRgba8(218, 165,  32)},
        {"gray",                 Color::fromRgba8(128, 128, 128)},
        {"green",                Color::fromRgba8(  0, 128,   0)},
        {"greenyellow",          Color::fromRgba8(173, 255,  47)},
        {"grey",                 Color::fromRgba8(128, 128, 128)},
        {"honeydew",             Color::fromRgba8(240, 255, 240)},
        {"hotpink",              Color::fromRgba8(255, 105, 180)},
        {"indianred",            Color::fromRgba8(205,  92,  92)},
        {"indigo",               Color::fromRgba8( 75,   0, 130)},
        {"ivory",                Color::fromRgba8(255, 255, 240)},
        {"khaki",                Color::fromRgba8(240, 230, 140)},
        {"lavender",             Color::fromRgba8(230, 230, 250)},
        {"lavenderblush",        Color::fromRgba8(255, 240, 245)},
        {"lawngreen",            Color::fromRgba8(124, 252,   0)},
        {"lemonchiffon",         Color::fromRgba8(255, 250, 205)},
        {"lightblue",            Color::fromRgba8(173, 216, 230)},
        {"lightcoral",           Color::fromRgba8(240, 128, 128)},
        {"lightcyan",            Color::fromRgba8(224, 255, 255)},
        {"lightgoldenrodyellow", Color::fromRgba8(250, 250, 210)},
        {"lightgray",            Color::fromRgba8(211, 211, 211)},
        {"lightgreen",           Color::fromRgba8(144, 238, 144)},
        {"lightgrey",            Color::fromRgba8(211, 211, 211)},
        {"lightpink",            Color::fromRgba8(255, 182, 193)},
        {"lightsalmon",          Color::fromRgba8(255, 160, 122)},
        {"lightseagreen",        Color::fromRgba8( 32, 178, 170)},
        {"lightskyblue",         Color::fromRgba8(135, 206, 250)},
        {"lightslategray",       Color::fromRgba8(119, 136, 153)},
        {"lightslategrey",       Color::fromRgba8(119, 136, 153)},
        {"lightsteelblue",       Color::fromRgba8(176, 196, 222)},
        {"lightyellow",          Color::fromRgba8(255, 255, 224)},
        {"lime",                 Color::fromRgba8(  0, 255,   0)},
        {"limegreen",            Color::fromRgba8( 50, 205,  50)},
        {"linen",                Color::fromRgba8(250, 240, 230)},
        {"magenta",              Color::fromRgba8(255,   0, 255)},
        {"maroon",               Color::fromRgba8(128,   0,   0)},
        {"mediumaquamarine",     Color::fromRgba8(102, 205, 170)},
        {"mediumblue",           Color::fromRgba8(  0,   0, 205)},
        {"mediumorchid",         Color::fromRgba8(186,  85, 211)},
        {"mediumpurple",         Color::fromRgba8(147, 112, 219)},
        {"mediumseagreen",       Color::fromRgba8( 60, 179, 113)},
        {"mediumslateblue",      Color::fromRgba8(123, 104, 238)},
        {"mediumspringgreen",    Color::fromRgba8(  0, 250, 154)},
        {"mediumturquoise",      Color::fromRgba8( 72, 209, 204)},
        {"mediumvioletred",      Color::fromRgba8(199,  21, 133)},
        {"midnightblue",         Color::fromRgba8( 25,  25, 112)},
        {"mintcream",            Color::fromRgba8(245, 255, 250)},
        {"mistyrose",            Color::fromRgba8(255, 228, 225)},
        {"moccasin",             Color::fromRgba8(255, 228, 181)},
        {"navajowhite",          Color::fromRgba8(255, 222, 173)},
        {"navy",                 Color::fromRgba8(  0,   0, 128)},
        {"oldlace",              Color::fromRgba8(253, 245, 230)},
        {"olive",                Color::fromRgba8(128, 128,   0)},
        {"olivedrab",            Color::fromRgba8(107, 142,  35)},
        {"orange",               Color::fromRgba8(255, 165,   0)},
        {"orangered",            Color::fromRgba8(255,  69,   0)},
        {"orchid",               Color::fromRgba8(218, 112, 214)},
        {"palegoldenrod",        Color::fromRgba8(238, 232, 170)},
        {"palegreen",            Color::fromRgba8(152, 251, 152)},
        {"paleturquoise",        Color::fromRgba8(175, 238, 238)},
        {"palevioletred",        Color::fromRgba8(219, 112, 147)},
        {"papayawhip",           Color::fromRgba8(255, 239, 213)},
        {"peachpuff",            Color::fromRgba8(255, 218, 185)},
        {"peru",                 Color::fromRgba8(205, 133,  63)},
        {"pink",                 Color::fromRgba8(255, 192, 203)},
        {"plum",                 Color::fromRgba8(221, 160, 221)},
        {"powderblue",           Color::fromRgba8(176, 224, 230)},
        {"purple",               Color::fromRgba8(128,   0, 128)},
        {"rebeccapurple",        Color::fromRgba8(102,  51, 153)},
        {"red",                  Color::fromRgba8(255,   0,   0)},
        {"rosybrown",            Color::fromRgba8(188, 143, 143)},
        {"royalblue",            Color::fromRgba8( 65, 105, 225)},
        {"saddlebrown",          Color::fromRgba8(139,  69,  19)},
        {"salmon",               Color::fromRgba8(250, 128, 114)},
        {"sandybrown",           Color::fromRgba8(244, 164,  96)},
        {"seagreen",             Color::fromRgba8( 46, 139,  87)},
        {"seashell",             Color::fromRgba8(255, 245, 238)},
        {"sienna",               Color::fromRgba8(160,  82,  45)},
        {"silver",               Color::fromRgba8(192, 192, 192)},
        {"skyblue",              Color::fromRgba8(135, 206, 235)},
        {"slateblue",            Color::fromRgba8(106,  90, 205)},
        {"slategray",            Color::fromRgba8(112, 128, 144)},
        {"slategrey",            Color::fromRgba8(112, 128, 144)},
        {"snow",                 Color::fromRgba8(255, 250, 250)},
        {"springgreen",          Color::fromRgba8(  0, 255, 127)},
        {"steelblue",            Color::fromRgba8( 70, 130, 180)},
        {"tan",                  Color::fromRgba8(210, 180, 140)},
        {"teal",                 Color::fromRgba8(  0, 128, 128)},
        {"thistle",              Color::fromRgba8(216, 191, 216)},
        {"tomato",               Color::fromRgba8(255,  99,  71)},
        {"turquoise",            Color::fromRgba8( 64, 224, 208)},
        {"violet",               Color::fromRgba8(238, 130, 238)},
        {"wheat",                Color::fromRgba8(245, 222, 179)},
        {"white",                Color::fromRgba8(255, 255, 255)},
        {"whitesmoke",           Color::fromRgba8(245, 245, 245)},
        {"yellow",               Color::fromRgba8(255, 255,   0)},
        {"yellowgreen",          Color::fromRgba8(154, 205,  50)},
        // tab: palette aliases
        {"tab:blue",             kTab10[0]},
        {"tab:orange",           kTab10[1]},
        {"tab:green",            kTab10[2]},
        {"tab:red",              kTab10[3]},
        {"tab:purple",           kTab10[4]},
        {"tab:brown",            kTab10[5]},
        {"tab:pink",             kTab10[6]},
        {"tab:gray",             kTab10[7]},
        {"tab:olive",            kTab10[8]},
        {"tab:cyan",             kTab10[9]},
    };
    return table;
}

// ─── Helpers ───────────────────────────────────────────────────────────────

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Parse a hex digit (0-9, a-f, A-F). Returns -1 on failure.
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// Parse a hex color string (without the leading '#').
/// Supports RGB, RRGGBB, RGBA, RRGGBBAA (3, 6, 4, 8 chars).
std::optional<Color> parseHex(std::string_view s) {
    auto hexByte = [](char hi, char lo) -> std::optional<uint8_t> {
        int h = hexDigit(hi), l = hexDigit(lo);
        if (h < 0 || l < 0) return std::nullopt;
        return static_cast<uint8_t>((h << 4) | l);
    };
    auto expand = [](char c) -> uint8_t {
        int v = hexDigit(c);
        return static_cast<uint8_t>(v * 17); // 0x0 -> 0x00, 0xf -> 0xff
    };
    switch (s.size()) {
        case 3: { // RGB
            int r = hexDigit(s[0]), g = hexDigit(s[1]), b = hexDigit(s[2]);
            if (r < 0 || g < 0 || b < 0) return std::nullopt;
            return Color::fromRgba8(expand(s[0]), expand(s[1]), expand(s[2]));
        }
        case 4: { // RGBA
            int r = hexDigit(s[0]), g = hexDigit(s[1]), b = hexDigit(s[2]), a = hexDigit(s[3]);
            if (r < 0 || g < 0 || b < 0 || a < 0) return std::nullopt;
            return Color::fromRgba8(expand(s[0]), expand(s[1]), expand(s[2]), expand(s[3]));
        }
        case 6: { // RRGGBB
            auto r = hexByte(s[0], s[1]);
            auto g = hexByte(s[2], s[3]);
            auto b = hexByte(s[4], s[5]);
            if (!r || !g || !b) return std::nullopt;
            return Color::fromRgba8(*r, *g, *b);
        }
        case 8: { // RRGGBBAA
            auto r = hexByte(s[0], s[1]);
            auto g = hexByte(s[2], s[3]);
            auto b = hexByte(s[4], s[5]);
            auto a = hexByte(s[6], s[7]);
            if (!r || !g || !b || !a) return std::nullopt;
            return Color::fromRgba8(*r, *g, *b, *a);
        }
        default:
            return std::nullopt;
    }
}

/// Parse a single-letter color shorthand (b, g, r, c, m, y, k, w).
std::optional<Color> parseShorthand(char c) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'b': return Color::fromRgba8(  0,   0, 255); // blue
        case 'g': return Color::fromRgba8(  0, 128,   0); // green
        case 'r': return Color::fromRgba8(255,   0,   0); // red
        case 'c': return Color::fromRgba8(  0, 255, 255); // cyan
        case 'm': return Color::fromRgba8(255,   0, 255); // magenta
        case 'y': return Color::fromRgba8(255, 255,   0); // yellow
        case 'k': return Color::fromRgba8(  0,   0,   0); // black
        case 'w': return Color::fromRgba8(255, 255, 255); // white
        default:  return std::nullopt;
    }
}

/// Parse a CN cycle color string ("C0" through "C9").
std::optional<Color> parseCycle(std::string_view s) {
    if (s.size() != 2 || (s[0] != 'C' && s[0] != 'c')) return std::nullopt;
    if (s[1] < '0' || s[1] > '9') return std::nullopt;
    return ColorCycle::at(static_cast<size_t>(s[1] - '0'));
}

/// Parse a grayscale string ("0.0" through "1.0").
std::optional<Color> parseGrayscale(std::string_view s) {
    // Must look like a float in [0, 1].
    // std::from_float is C++17; use strtof for portability.
    std::string tmp(s);
    char* end = nullptr;
    float v = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0') return std::nullopt;
    if (v < 0.0f || v > 1.0f) return std::nullopt;
    uint8_t g = static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
    return Color::fromRgba8(g, g, g);
}

} // namespace

// ─── Public API ────────────────────────────────────────────────────────────

std::optional<Color> Color::parse(std::string_view s) {
    // Trim whitespace.
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty()) return std::nullopt;

    // Hex: starts with '#'
    if (s[0] == '#') {
        return parseHex(s.substr(1));
    }

    // Single-letter shorthand (case-insensitive)
    if (s.size() == 1) {
        if (auto c = parseShorthand(s[0])) return c;
    }

    // CN cycle color: "C0"–"C9"
    if (s.size() == 2 && (s[0] == 'C' || s[0] == 'c')) {
        if (auto c = parseCycle(s)) return c;
    }

    // Grayscale: a float string in [0, 1]
    // (check before named colors since "0.5" isn't a name)
    if (s[0] >= '0' && s[0] <= '9') {
        if (auto c = parseGrayscale(s)) return c;
    }

    // Named color (case-insensitive)
    auto lower = toLower(s);
    const auto& table = namedColors();
    auto it = table.find(lower);
    if (it != table.end()) return it->second;

    return std::nullopt;
}

Color Color::parseOr(std::string_view s, Color fallback) {
    auto r = parse(s);
    return r.value_or(fallback);
}

Color ColorCycle::at(size_t i) {
    return kTab10[i % kTab10.size()];
}

} // namespace volcano::plot
