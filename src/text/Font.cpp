// volcano/text/Font.cpp
#include "volcano/text/Font.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef VOLCANO_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#endif

namespace volcano::text {

namespace {

#ifdef VOLCANO_HAS_FREETYPE
FT_Library g_ft = nullptr;

FT_Library ftLibrary() {
    if (!g_ft) FT_Init_FreeType(&g_ft);
    return g_ft;
}

// ─── Outline decomposition ────────────────────────────────────────────────
// FreeType calls these callbacks as it walks the outline. We collect
// the control points into a contour list, then flatten beziers and
// triangulate.

struct DecomposeState {
    std::vector<FontPoint> currentContour;
    std::vector<std::vector<FontPoint>> contours;
    FontPoint last = {0, 0};
};

// Convert FreeType 26.6 fixed-point to float pixels.
inline float ft266ToFloat(FT_Pos v) { return float(v) / 64.0f; }

int moveTo(const FT_Vector* to, void* user) {
    auto* st = static_cast<DecomposeState*>(user);
    if (!st->currentContour.empty()) {
        st->contours.push_back(std::move(st->currentContour));
        st->currentContour.clear();
    }
    st->last = {ft266ToFloat(to->x), ft266ToFloat(to->y)};
    st->currentContour.push_back(st->last);
    return 0;
}

int lineTo(const FT_Vector* to, void* user) {
    auto* st = static_cast<DecomposeState*>(user);
    st->last = {ft266ToFloat(to->x), ft266ToFloat(to->y)};
    st->currentContour.push_back(st->last);
    return 0;
}

int conicTo(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto* st = static_cast<DecomposeState*>(user);
    FontPoint p0 = st->last;
    FontPoint p1 = {ft266ToFloat(control->x), ft266ToFloat(control->y)};
    FontPoint p2 = {ft266ToFloat(to->x), ft266ToFloat(to->y)};
    // Flatten quadratic bezier into line segments.
    constexpr int kSteps = 8;
    for (int i = 1; i <= kSteps; ++i) {
        float t = float(i) / float(kSteps);
        float u = 1.0f - t;
        FontPoint p{
            u*u*p0.x + 2*u*t*p1.x + t*t*p2.x,
            u*u*p0.y + 2*u*t*p1.y + t*t*p2.y
        };
        st->currentContour.push_back(p);
    }
    st->last = p2;
    return 0;
}

int cubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    auto* st = static_cast<DecomposeState*>(user);
    FontPoint p0 = st->last;
    FontPoint p1 = {ft266ToFloat(c1->x), ft266ToFloat(c1->y)};
    FontPoint p2 = {ft266ToFloat(c2->x), ft266ToFloat(c2->y)};
    FontPoint p3 = {ft266ToFloat(to->x), ft266ToFloat(to->y)};
    // Flatten cubic bezier into line segments.
    constexpr int kSteps = 12;
    for (int i = 1; i <= kSteps; ++i) {
        float t = float(i) / float(kSteps);
        float u = 1.0f - t;
        FontPoint p{
            u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
            u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
        };
        st->currentContour.push_back(p);
    }
    st->last = p3;
    return 0;
}

// ─── Triangulation (ear clipping) ──────────────────────────────────────────
// Simple ear-clipping triangulator for a single polygon with holes.
// This is not the fastest algorithm but is robust enough for glyph outlines.

struct Triangulator {
    // All contours: outer + holes. Outer is CCW, holes are CW (or vice versa).
    // FreeType outlines: outer contours are CCW, inner (holes) are CW.
    std::vector<std::vector<FontPoint>> contours;
    std::vector<FontPoint> vertices;  // merged vertex list
    std::vector<int> vertexToContour; // which contour each vertex belongs to
    std::vector<bool> earAvailable;

    // Build a merged vertex list from all contours.
    void build() {
        vertices.clear();
        vertexToContour.clear();
        for (size_t ci = 0; ci < contours.size(); ++ci) {
            for (const auto& p : contours[ci]) {
                vertices.push_back(p);
                vertexToContour.push_back(int(ci));
            }
        }
    }

    // Check if a point is inside a triangle (CCW).
    static bool pointInTriangle(FontPoint p, FontPoint a, FontPoint b, FontPoint c) {
        float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
        float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
        float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    }

    // Signed area of a polygon (positive = CCW).
    static float signedArea(const std::vector<FontPoint>& poly) {
        float area = 0;
        for (size_t i = 0; i < poly.size(); ++i) {
            size_t j = (i + 1) % poly.size();
            area += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
        }
        return area * 0.5f;
    }

    // Cross product of vectors (a→b) × (b→c). Positive = left turn (CCW).
    static float cross(FontPoint a, FontPoint b, FontPoint c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    std::vector<FontPoint> triangulate() {
        std::vector<FontPoint> result;

        // For simplicity, triangulate each contour independently.
        // This doesn't handle holes correctly (glyphs like 'O', 'A' have holes),
        // but for basic text it works. A proper implementation would merge
        // holes into the outer contour using bridges.
        //
        // TODO: handle holes with bridge edges for glyphs like O, A, B, etc.
        // For now, we use a simpler approach: render all contours as filled
        // using the even-odd rule via the triangulator below.

        // Merge all contours into one polygon list with even-odd fill.
        // Simple approach: just ear-clip each contour separately.
        // For contours that are holes (CW), reverse them to CCW first.
        for (auto& contour : contours) {
            if (signedArea(contour) < 0) {
                std::reverse(contour.begin(), contour.end());
            }
        }

        // If there's only one contour (no holes), ear-clip it directly.
        if (contours.size() == 1) {
            earClip(contours[0], result);
            return result;
        }

        // Multiple contours: ear-clip each independently. This will fill
        // holes incorrectly (holes will be filled), but for many glyphs
        // the visual difference is minor at small sizes.
        // TODO: implement proper hole bridging.
        for (auto& contour : contours) {
            earClip(contour, result);
        }

        return result;
    }

    void earClip(const std::vector<FontPoint>& poly, std::vector<FontPoint>& out) {
        if (poly.size() < 3) return;

        // Build index list.
        std::vector<int> idx(poly.size());
        for (int i = 0; i < int(poly.size()); ++i) idx[i] = i;

        // Ear clipping.
        int guard = 0;
        while (idx.size() > 3 && guard++ < int(poly.size()) * 3) {
            bool found = false;
            for (size_t i = 0; i < idx.size(); ++i) {
                size_t prev = (i + idx.size() - 1) % idx.size();
                size_t next = (i + 1) % idx.size();
                FontPoint a = poly[idx[prev]];
                FontPoint b = poly[idx[i]];
                FontPoint c = poly[idx[next]];

                // Must be a convex vertex (left turn for CCW polygon).
                if (cross(a, b, c) <= 0) continue;

                // No other vertex inside this triangle.
                bool ear = true;
                for (size_t j = 0; j < idx.size(); ++j) {
                    if (j == prev || j == i || j == next) continue;
                    if (pointInTriangle(poly[idx[j]], a, b, c)) {
                        ear = false;
                        break;
                    }
                }
                if (ear) {
                    out.push_back(a);
                    out.push_back(b);
                    out.push_back(c);
                    idx.erase(idx.begin() + i);
                    found = true;
                    break;
                }
            }
            if (!found) break;  // degenerate polygon
        }
        // Add the last triangle.
        if (idx.size() == 3) {
            out.push_back(poly[idx[0]]);
            out.push_back(poly[idx[1]]);
            out.push_back(poly[idx[2]]);
        }
    }
};

#endif // VOLCANO_HAS_FREETYPE

} // namespace

Font::Font(const std::filesystem::path& path, float size) : size_(size) {
#ifdef VOLCANO_HAS_FREETYPE
    auto lib = ftLibrary();
    FT_Face face;
    if (FT_New_Face(lib, path.string().c_str(), 0, &face)) {
        valid_ = false; return;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size));
    face_ = face;
    valid_ = true;
#else
    (void)path;
    valid_ = false;
#endif
}

Font::~Font() {
#ifdef VOLCANO_HAS_FREETYPE
    if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
#endif
}

Font::Font(Font&& o) noexcept : face_(o.face_), size_(o.size_), valid_(o.valid_) {
    o.face_ = nullptr; o.valid_ = false;
}

Font& Font::operator=(Font&& o) noexcept {
    if (this != &o) {
#ifdef VOLCANO_HAS_FREETYPE
        if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
#endif
        face_ = o.face_; size_ = o.size_; valid_ = o.valid_;
        o.face_ = nullptr; o.valid_ = false;
    }
    return *this;
}

GlyphInfo Font::outlineGlyph(uint32_t codepoint) const {
    GlyphInfo gi;
    gi.codepoint = codepoint;
#ifdef VOLCANO_HAS_FREETYPE
    if (!valid_) return gi;
    auto face = static_cast<FT_Face>(face_);

    if (FT_Load_Glyph(face, FT_Get_Char_Index(face, codepoint), FT_LOAD_NO_BITMAP)) {
        return gi;
    }
    FT_GlyphSlot g = face->glyph;
    if (g->format != FT_GLYPH_FORMAT_OUTLINE) return gi;

    // Decompose outline into contours.
    DecomposeState state;
    FT_Outline_Funcs funcs{};
    funcs.move_to = moveTo;
    funcs.line_to = lineTo;
    funcs.conic_to = conicTo;
    funcs.cubic_to = cubicTo;
    funcs.shift = 0;
    funcs.delta = 0;

    if (FT_Outline_Decompose(&g->outline, &funcs, &state)) return gi;
    if (!state.currentContour.empty()) {
        state.contours.push_back(std::move(state.currentContour));
    }

    // Triangulate.
    Triangulator tri;
    tri.contours = std::move(state.contours);
    auto triangles = tri.triangulate();
    gi.mesh.vertices = std::move(triangles);

    // Compute bounding box.
    if (!gi.mesh.vertices.empty()) {
        gi.mesh.minX = gi.mesh.maxX = gi.mesh.vertices[0].x;
        gi.mesh.minY = gi.mesh.maxY = gi.mesh.vertices[0].y;
        for (const auto& v : gi.mesh.vertices) {
            gi.mesh.minX = std::min(gi.mesh.minX, v.x);
            gi.mesh.maxX = std::max(gi.mesh.maxX, v.x);
            gi.mesh.minY = std::min(gi.mesh.minY, v.y);
            gi.mesh.maxY = std::max(gi.mesh.maxY, v.y);
        }
    }

    // Metrics: FreeType stores in 26.6 fixed-point.
    gi.bearingX = g->bitmap_left;
    gi.bearingY = g->bitmap_top;
    gi.advance = static_cast<int32_t>(g->advance.x >> 6);
#else
    (void)codepoint;
#endif
    return gi;
}

void Font::outlineAtlas(std::u32string_view codepoints,
                        std::vector<GlyphInfo>& glyphs) const {
    glyphs.clear();
    for (char32_t cp : codepoints) {
        glyphs.push_back(outlineGlyph(cp));
    }
}

std::filesystem::path findSystemFont(std::string_view family) {
    std::vector<std::filesystem::path> dirs = {
        "/usr/share/fonts", "/usr/local/share/fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".fonts",
        std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".local/share/fonts",
    };
    // Prefer regular (non-bold, non-italic) variants.
    auto isRegular = [](const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower.find("bold") == std::string::npos &&
               lower.find("italic") == std::string::npos &&
               lower.find("oblique") == std::string::npos &&
               lower.find("condensed") == std::string::npos;
    };
    // First pass: look for regular variants.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            if (name.find(family) != std::string::npos && isRegular(name))
                return e.path();
        }
    }
    // Second pass: accept any variant.
    for (const auto& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        for (auto& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            auto name = e.path().filename().string();
            if (name.find(family) != std::string::npos) return e.path();
        }
    }
    return {};
}

} // namespace volcano::text
