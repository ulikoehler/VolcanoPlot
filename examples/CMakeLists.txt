# examples/CMakeLists.txt

if(VOLCANO_BUILD_SCREEN_BACKEND)
    add_executable(example_chirp_liveplot chirp_liveplot.cpp)
    target_link_libraries(example_chirp_liveplot PRIVATE
        volcano_backend volcano_render volcano_plot volcano_text volcano_encode
    )
endif()

add_executable(bench_line bench_line.cpp)
target_link_libraries(bench_line PRIVATE
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

add_executable(example_gallery gallery.cpp)
target_link_libraries(example_gallery PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(example_microgallery microgallery.cpp)
target_link_libraries(example_microgallery PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(example_bench_matrix bench_matrix.cpp)
target_link_libraries(example_bench_matrix PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

add_executable(bench_encode bench_encode.cpp)
target_link_libraries(bench_encode PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)
