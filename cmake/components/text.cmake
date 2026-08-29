# Component: volcano_text
# SDF glyph atlas text rendering for axis labels, legends, titles.

set(VOLCANO_TEXT_SOURCES
    ${VOLCANO_ROOT}/src/text/TextRenderer.cpp
    ${VOLCANO_ROOT}/src/text/GlyphAtlas.cpp
    ${VOLCANO_ROOT}/src/text/Font.cpp
)

volcano_add_component(volcano_text
    SOURCES ${VOLCANO_TEXT_SOURCES}
    PUBLIC_LINK volcano_core
    PRIVATE_LINK ${VOLCANO_FREETYPE_LINK}
    PUBLIC_INC include/volcano/text
    PUBLIC_DEFS
        $<$<BOOL:${VOLCANO_HAS_FREETYPE}>:VOLCANO_HAS_FREETYPE=1>
)
