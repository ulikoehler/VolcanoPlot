# Component: volcano_plot
# Plot data model, axes, transforms, styles (matplotlib-style), plot types.

set(VOLCANO_PLOT_SOURCES
    ${VOLCANO_ROOT}/src/plot/Plot.cpp
    ${VOLCANO_ROOT}/src/plot/Axes.cpp
    ${VOLCANO_ROOT}/src/plot/Transform.cpp
    ${VOLCANO_ROOT}/src/plot/Style.cpp
    ${VOLCANO_ROOT}/src/plot/Colormap.cpp
    ${VOLCANO_ROOT}/src/plot/ColormapData.cpp
    ${VOLCANO_ROOT}/src/plot/Color.cpp
    ${VOLCANO_ROOT}/src/plot/DataSeries.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ScatterPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/LinePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BarPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/GroupedBarPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BarLabelPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BrokenBarHPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/FillPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/FillBetweenPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/HistPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Hist2DPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/HexbinPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ErrorbarPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BoxPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ReferenceLines.cpp
    ${VOLCANO_ROOT}/src/plot/plots/PiePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/HeatmapPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/MatshowPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/PcolormeshPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/PcolorfastPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/SpyPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/FigImagePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ContourPlot.cpp
    ${VOLCANO_ROOT}/src/plot/Triangulation.cpp
    ${VOLCANO_ROOT}/src/plot/plots/TriContourPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/TripcolorPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/TriplotPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/BarbsPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/StepPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ECDFPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/StackPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ViolinPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/QuiverPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/StreamPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/XCorrPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/StemPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/SpectrumPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/PsdPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/CsdPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/CoherePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/SpecgramPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Plot3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Scatter3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Bar3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Errorbar3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/WireframePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/TrisurfPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Contour3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/TricontourPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Quiver3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/VoxelsPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Text3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/Collections3D.cpp
    ${VOLCANO_ROOT}/src/plot/plots/ChirpPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/MexicanHatPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/VolcanoPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/SurfacePlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/KDEPlot.cpp
    ${VOLCANO_ROOT}/src/plot/plots/FunctionPlot.cpp
)

volcano_add_component(volcano_plot
    SOURCES ${VOLCANO_PLOT_SOURCES}
    PUBLIC_LINK volcano_core
    PRIVATE_LINK volcano_render
    PUBLIC_INC include/volcano/plot
)
