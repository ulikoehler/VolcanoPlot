# Component: volcano_backend
# Screen (SDL3 + swapchain) and headless offscreen backends.
# Both implement the IBackend interface so plot code is backend-agnostic.

set(VOLCANO_BACKEND_SOURCES
    ${VOLCANO_ROOT}/src/backend/Backend.cpp
)

if(VOLCANO_BUILD_SCREEN_BACKEND)
    list(APPEND VOLCANO_BACKEND_SOURCES
        ${VOLCANO_ROOT}/src/backend/ScreenBackend.cpp
    )
endif()

if(VOLCANO_BUILD_HEADLESS_BACKEND)
    list(APPEND VOLCANO_BACKEND_SOURCES
        ${VOLCANO_ROOT}/src/backend/HeadlessBackend.cpp
    )
endif()

set(_public_link volcano_core)
if(VOLCANO_BUILD_SCREEN_BACKEND)
    list(APPEND _public_link SDL3::SDL3)
endif()

volcano_add_component(volcano_backend
    SOURCES ${VOLCANO_BACKEND_SOURCES}
    PUBLIC_LINK ${_public_link}
    PUBLIC_INC include/volcano/backend
    PUBLIC_DEFS
        $<$<BOOL:${VOLCANO_BUILD_SCREEN_BACKEND}>:VOLCANO_HAS_SCREEN_BACKEND=1>
        $<$<BOOL:${VOLCANO_BUILD_HEADLESS_BACKEND}>:VOLCANO_HAS_HEADLESS_BACKEND=1>
)
