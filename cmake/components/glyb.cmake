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
)

add_library(glyb_static STATIC ${GLYB_SOURCES})
target_compile_features(glyb_static PRIVATE cxx_std_17)

# glyb headers expect to be found via plain #include "glyph.h" etc.
target_include_directories(glyb_static PUBLIC
    ${GLYB_PATH}/src
    ${GLYB_PATH}/third_party/glm
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
