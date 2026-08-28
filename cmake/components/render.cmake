# Component: volcano_render
# Render passes, pipelines, MSAA, frame orchestration.

set(VOLCANO_RENDER_SOURCES
    ${VOLCANO_ROOT}/src/render/RenderPass.cpp
    ${VOLCANO_ROOT}/src/render/Frame.cpp
    ${VOLCANO_ROOT}/src/render/Renderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/PointRenderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/LineRenderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/BarRenderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/PieRenderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/HeatmapRenderer.cpp
    ${VOLCANO_ROOT}/src/render/primitives/SurfaceRenderer.cpp
    ${VOLCANO_ROOT}/src/render/GridRenderer.cpp
)

volcano_add_component(volcano_render
    SOURCES ${VOLCANO_RENDER_SOURCES}
    PUBLIC_LINK volcano_core volcano_plot
    PUBLIC_INC include/volcano/render
)
