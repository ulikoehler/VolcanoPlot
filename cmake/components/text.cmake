# Component: volcano_text
# glyb-based bitmap atlas text rendering for axis labels, legends, titles.

set(VOLCANO_TEXT_SOURCES
    ${VOLCANO_ROOT}/src/text/TextRenderer.cpp
)

volcano_add_component(volcano_text
    SOURCES ${VOLCANO_TEXT_SOURCES}
    PUBLIC_LINK volcano_core
    PRIVATE_LINK glyb_static
    PUBLIC_INC include/volcano/text
)
