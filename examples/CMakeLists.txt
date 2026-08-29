# examples/CMakeLists.txt

add_executable(example_chirp_liveplot chirp_liveplot.cpp)
target_link_libraries(example_chirp_liveplot PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(example_headless_scatter headless_scatter.cpp)
target_link_libraries(example_headless_scatter PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(example_volcano_plot volcano_plot.cpp)
target_link_libraries(example_volcano_plot PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(example_showcase showcase.cpp)
target_link_libraries(example_showcase PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)
