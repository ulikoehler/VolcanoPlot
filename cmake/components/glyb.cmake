# Component: glyb (third-party text rendering library)
# Builds glyb's core source files as a static library, linking against
# system FreeType and HarfBuzz, and glyb's bundled glm.

set(GLYB_SOURCES
    ${GLYB_PATH}/src/binpack.cc
    ${GLYB_PATH}/src/file.cc
    ${GLYB_PATH}/src/font.cc
    ${GLYB_PATH}/src/glyph.cc
    ${GLYB_PATH}/src/image.cc
    ${GLYB_PATH}/src/logger.cc
    ${GLYB_PATH}/src/utf8.cc
    ${VOLCANO_ROOT}/src/text/glyb_msdf_stub.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/Contour.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/EdgeHolder.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/Scanline.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/Shape.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/SignedDistance.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/Vector2.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/contour-combiners.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/edge-coloring.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/edge-segments.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/edge-selectors.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/equation-solver.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/estimate-sdf-error.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/msdfgen.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/rasterization.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/render-sdf.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/save-bmp.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/save-tiff.cpp
    ${GLYB_PATH}/third_party/msdfgen/core/shape-description.cpp
)

add_library(glyb_static STATIC ${GLYB_SOURCES})
target_compile_features(glyb_static PRIVATE cxx_std_17)

# glyb headers expect to be found via plain #include "glyph.h" etc.
target_include_directories(glyb_static PUBLIC
    ${GLYB_PATH}/src
    ${GLYB_PATH}/third_party/glm
    ${GLYB_PATH}/third_party/msdfgen
)

# System FreeType and HarfBuzz
target_link_libraries(glyb_static PUBLIC
    ${VOLCANO_FREETYPE_LINK}
    ${VOLCANO_HARFBUZZ_LINK}
    PNG::PNG
    Threads::Threads
)

# Suppress warnings from third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(glyb_static PRIVATE -w)
endif()

# Alias for consistency
add_library(glyb::glyb ALIAS glyb_static)
