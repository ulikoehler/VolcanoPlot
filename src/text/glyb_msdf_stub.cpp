// glyb_msdf_stub.cpp — real glyph_renderer_msdf backed by the bundled
// msdfgen core (replaces the original no-op stub; glyb's own msdf.cc
// pulls in msdfgen-ext + tinyxml2, which we don't need — we drive
// FreeType outline decomposition ourselves).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include "msdfgen.h"

// glyb headers must be included in dependency order
#include "binpack.h"
#include "utf8.h"
#include "image.h"
#include "draw.h"
#include "font.h"
#include "glyph.h"
#include "msdf.h"

namespace {

struct FtContext {
    msdfgen::Shape* shape;
    msdfgen::Point2 position;
    msdfgen::Contour* contour;
};

msdfgen::Point2 ftPoint2(const FT_Vector& v) {
    return msdfgen::Point2(v.x / 64.0, v.y / 64.0);
}

int ftMoveTo(const FT_Vector* to, void* user) {
    auto* c = static_cast<FtContext*>(user);
    c->contour = &c->shape->addContour();
    c->position = ftPoint2(*to);
    return 0;
}

int ftLineTo(const FT_Vector* to, void* user) {
    auto* c = static_cast<FtContext*>(user);
    c->contour->addEdge(new msdfgen::LinearSegment(c->position,
                                                   ftPoint2(*to)));
    c->position = ftPoint2(*to);
    return 0;
}

int ftConicTo(const FT_Vector* ctrl, const FT_Vector* to, void* user) {
    auto* c = static_cast<FtContext*>(user);
    c->contour->addEdge(new msdfgen::QuadraticSegment(c->position,
                                                      ftPoint2(*ctrl),
                                                      ftPoint2(*to)));
    c->position = ftPoint2(*to);
    return 0;
}

int ftCubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to,
              void* user) {
    auto* c = static_cast<FtContext*>(user);
    c->contour->addEdge(new msdfgen::CubicSegment(c->position, ftPoint2(*c1),
                                                  ftPoint2(*c2),
                                                  ftPoint2(*to)));
    c->position = ftPoint2(*to);
    return 0;
}

} // namespace

atlas_entry glyph_renderer_msdf::render(font_atlas* atlas,
                                        font_face_ft* face, int font_size,
                                        int glyph)
{
    // Same scheme as glyb's msdf.cc: rasterize at a fixed reference size
    // so one atlas entry serves all render scales.
    const int char_height = 128 * 64;
    const double range = 8.0;
    msdfgen::Shape shape;
    FtContext context{&shape, {}, nullptr};

    if (FT_Set_Char_Size(face->ftface, 0, char_height, font_manager::dpi,
                         font_manager::dpi))
        return atlas_entry(-1);
    if (FT_Load_Glyph(face->ftface, glyph, FT_LOAD_NO_HINTING))
        return atlas_entry(-1);

    FT_Outline_Funcs ftFuncs{};
    ftFuncs.move_to = ftMoveTo;
    ftFuncs.line_to = ftLineTo;
    ftFuncs.conic_to = ftConicTo;
    ftFuncs.cubic_to = ftCubicTo;
    if (FT_Outline_Decompose(&face->ftface->glyph->outline, &ftFuncs,
                             &context))
        return atlas_entry(-1);

    FT_GlyphSlot ftglyph = face->ftface->glyph;
    int ox = int(std::floor(float(ftglyph->metrics.horiBearingX) / 64.f)) - 1;
    int oy = int(std::floor(float(ftglyph->metrics.horiBearingY -
                                  ftglyph->metrics.height) / 64.f)) - 1;
    int w = int(std::ceil(ftglyph->metrics.width / 64.f)) + 2;
    int h = int(std::ceil(ftglyph->metrics.height / 64.f)) + 2;
    msdfgen::Vector2 translate(-ox, -oy), scale(1, 1);

    msdfgen::Bitmap<float, 3> msdf(w, h);
    msdfgen::edgeColoringSimple(shape, 3.0, 0);
    msdfgen::generateMSDF(msdf, shape, range, scale, translate, 0, true);
    msdfgen::distanceSignCorrection(msdf, shape, scale, translate,
                                    msdfgen::FILL_NONZERO);
    msdfgen::msdfErrorCorrection(msdf, 1.001 / (scale * range));

    atlas_entry ae = atlas->create(face, 0, glyph, char_height,
                                   ox, oy, w, h);
    if (ae.bin_id >= 0)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int r = msdfgen::pixelFloatToByte(msdf(x, y)[0]);
                int g = msdfgen::pixelFloatToByte(msdf(x, y)[1]);
                int b = msdfgen::pixelFloatToByte(msdf(x, y)[2]);
                uint32_t* dst = reinterpret_cast<uint32_t*>(
                    &atlas->pixels[((ae.y + y) * atlas->width + ae.x + x) * 4]);
                *dst = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16)
                     | 0xff000000u;
            }

    // Clients expect metrics for the requested font size to be loaded.
    face->get_metrics(font_size);
    return ae;
}
