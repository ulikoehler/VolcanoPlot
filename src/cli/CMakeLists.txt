# src/cli/CMakeLists.txt — CLI applications

# Screen liveplot
add_executable(volcano_screen main_screen.cpp)
target_link_libraries(volcano_screen PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)
if(VOLCANO_RUNTIME_SHADER_COMPILE)
    target_link_libraries(volcano_screen PRIVATE ${VOLCANO_SHADERC_LINK})
    target_compile_definitions(volcano_screen PRIVATE VOLCANO_RUNTIME_SHADER_COMPILE=1)
endif()

# Headless export
add_executable(volcano_headless main_headless.cpp)
target_link_libraries(volcano_headless PRIVATE
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)
if(VOLCANO_RUNTIME_SHADER_COMPILE)
    target_link_libraries(volcano_headless PRIVATE ${VOLCANO_SHADERC_LINK})
    target_compile_definitions(volcano_headless PRIVATE VOLCANO_RUNTIME_SHADER_COMPILE=1)
endif()
