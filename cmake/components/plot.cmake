# Component: volcano_plot
# Plot data model, axes, transforms, styles (matplotlib-style), plot types.

set(VOLCANO_PLOT_SOURCES
    ${VOLCANO_ROOT}/src/plot/Plot.cpp
    ${VOLCANO_ROOT}/src/plot/Axes.cpp
    ${VOLCANO_ROOT}/src/plot/Transform.cpp
    ${VOLCANO_ROOT}/src/plot/Style.cpp
    ${VOLCANO_ROOT}/src/plot/Colormap.cpp
    ${VOLCANO_ROOT}/src/plot/DataSeries.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ScatterPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/LinePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BarPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/PiePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/HeatmapPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/VolcanoPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/SurfacePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/KDEPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/FunctionPlot.cpp
)

volcano_add_component(volcano_plot
    SOURCES ${VOLCANO_PLOT_SOURCES}
    PUBLIC_LINK volcano_core
    PUBLIC_INC include/volcano/plot
)
