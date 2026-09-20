# python.cmake — pybind11 `volcanoplot` Python module
#
# Builds a `volcanoplot` extension module exposing the headless rendering
# path (Figure/Axes/plot/scatter/bar/imshow/savefig).
# Requires Python3 development headers; pybind11 comes from FetchContent.

find_package(Python3 COMPONENTS Interpreter Development.Module QUIET)
if(NOT Python3_Development.Module_FOUND)
    message(WARNING
        "VOLCANO_BUILD_PYTHON: Python3 development files not found — "
        "skipping the volcanoplot module")
    return()
endif()

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(volcanoplot
    ${CMAKE_CURRENT_SOURCE_DIR}/python/volcanoplot.cpp
)

target_link_libraries(volcanoplot PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

set_target_properties(volcanoplot PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
)

message(STATUS "volcanoplot Python module: ${Python3_VERSION} -> build/python/")
