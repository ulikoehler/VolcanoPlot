# cmake/web.cmake — Emscripten build of the web/WASM target (VOLCANO_WEB)
#
# Usage:
#   emcmake cmake -B build-web -DVOLCANO_WEB=ON -DCMAKE_BUILD_TYPE=Release
#   emmake cmake --build build-web -j4
#
# Produces web/dist/volcanoplot.{js,wasm} — the op-stream engine the TS
# interpreter (web/) replays through WebGPU.

if (NOT DEFINED ENV{EMSDK})
    message(WARNING "VOLCANO_WEB requires the Emscripten toolchain "
                    "(emcmake); skipping web target")
    return()
endif()

set(VOLCANO_WEB_SOURCES
    ${VOLCANO_ROOT}/src/web/OpStream.cpp
    ${VOLCANO_ROOT}/src/web/OpGpuServices.cpp
    ${VOLCANO_ROOT}/src/web/OpRenderers.cpp
    ${VOLCANO_ROOT}/src/web/OpRenderers2.cpp
    ${VOLCANO_ROOT}/src/web/OpRenderers3.cpp
    ${VOLCANO_ROOT}/src/web/OpRenderers4.cpp
    ${VOLCANO_ROOT}/src/web/bindings.cpp
    # backend-neutral render engine pieces (no volcano_core/backend)
    ${VOLCANO_ROOT}/src/render/TickLayout.cpp
    ${VOLCANO_ROOT}/src/render/VectorWriters.cpp
    ${VOLCANO_ROOT}/src/render/VectorRenderer.cpp
    ${VOLCANO_ROOT}/src/render/Renderer.cpp   # web-safe once vk:: is gone
    ${VOLCANO_ROOT}/src/render/Offload.cpp
    ${VOLCANO_ROOT}/src/render/primitives/SpineRendererBase.cpp
)

add_executable(volcanoplot_web ${VOLCANO_WEB_SOURCES})
target_compile_definitions(volcanoplot_web PRIVATE VOLCANO_WEB=1)
target_include_directories(volcanoplot_web PRIVATE
    ${VOLCANO_ROOT}/include ${VOLCANO_ROOT}/src)
target_compile_features(volcanoplot_web PRIVATE cxx_std_23)

target_link_options(volcanoplot_web PRIVATE
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=VolcanoPlot
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=4GB
    -sENVIRONMENT=web,node
    --bind
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32"
    # Font assets → MEMFS /fonts (TextRenderer searches /fonts on
    # Emscripten; no system font dirs exist in WASM).
    "--preload-file=${VOLCANO_ROOT}/dependencies/glyb/fonts/DejaVuSans.ttf@/fonts/DejaVuSans.ttf"
    "--preload-file=${VOLCANO_ROOT}/dependencies/glyb/fonts/DejaVuSerif.ttf@/fonts/DejaVuSerif.ttf"
    -fexceptions
    -O3
)
# Link the archive files directly — the component targets carry native
# link deps (volcano_core/volcano_render) that don't exist in this build.
add_dependencies(volcanoplot_web
    volcano_plot_static volcano_text_static glyb_static)
target_link_libraries(volcanoplot_web PRIVATE
    $<TARGET_FILE:volcano_plot_static>
    $<TARGET_FILE:volcano_text_static>
    $<TARGET_FILE:glyb_static>)
set_target_properties(volcanoplot_web PROPERTIES
    OUTPUT_NAME volcanoplot
    RUNTIME_OUTPUT_DIRECTORY ${VOLCANO_ROOT}/web/dist
    SUFFIX .js)
