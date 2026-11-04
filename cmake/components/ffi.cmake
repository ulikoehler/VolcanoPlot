# Component: volcano_ffi
# C ABI for embedding headless VolcanoPlot rendering into non-C++ hosts
# (e.g. liveplot-rs). Single TU, links the full static stack.

volcano_add_component(volcano_ffi
    SOURCES ${VOLCANO_ROOT}/src/ffi/volcano_ffi.cpp
    PUBLIC_LINK volcano_render volcano_backend
    PUBLIC_INC include/volcano/ffi
)
