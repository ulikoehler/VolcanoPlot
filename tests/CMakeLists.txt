# tests/CMakeLists.txt — GoogleTest

# Collect all test sources
file(GLOB VOLCANO_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(volcano_tests ${VOLCANO_TEST_SOURCES})
target_link_libraries(volcano_tests PRIVATE
    GTest::gtest_main
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

target_include_directories(volcano_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(volcano_tests)
