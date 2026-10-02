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
    # backend-neutral engine (no volcano_core/volcano_backend natives)
    ${VOLCANO_PLOT_SOURCES}
    ${VOLCANO_TEXT_SOURCES}
    # VectorRenderer + tick/layout etc. are engine-side too
    ${VOLCANO_ROOT}/src/render/TickLayout.cpp
    ${VOLCANO_ROOT}/src/render/VectorWriters.cpp
    ${VOLCANO_ROOT}/src/render/VectorRenderer.cpp
    ${VOLCANO_ROOT}/src/render/Renderer.cpp   # web-safe once vk:: is gone
)

add_executable(volcanoplot_web ${VOLCANO_WEB_SOURCES})
target_compile_definitions(volcanoplot_web PRIVATE VOLCANO_WEB=1)
target_include_directories(volcanoplot_web PRIVATE
    ${VOLCANO_ROOT}/include ${VOLCANO_ROOT}/src)
target_compile_features(volcanoplot_web PRIVATE cxx_std_23)

target_link_options(volcanoplot_web PRIVATE
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_NAME=VolcanoPlot
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=4GB
    -sENVIRONMENT=web
    --bind
    -fexceptions
    -O3
)
set_target_properties(volcanoplot_web PROPERTIES
    OUTPUT_NAME volcanoplot
    RUNTIME_OUTPUT_DIRECTORY ${VOLCANO_ROOT}/web/dist
    SUFFIX .js)
