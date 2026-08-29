// glyb_msdf_stub.cpp — stub for glyph_renderer_msdf (we don't use MSDF rendering)
// Provides the vtable and a no-op render() so the linker is satisfied
// without requiring the msdfgen library.

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// glyb headers must be included in dependency order
#include "binpack.h"
#include "utf8.h"
#include "image.h"
#include "draw.h"
#include "font.h"
#include "glyph.h"
#include "msdf.h"

atlas_entry glyph_renderer_msdf::render(font_atlas* /*atlas*/,
    font_face_ft* /*face*/, int /*font_size*/, int /*glyph*/)
{
    return atlas_entry(-1);
}
