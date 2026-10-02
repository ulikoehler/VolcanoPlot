# tests/CMakeLists.txt — GoogleTest

# Collect all test sources
file(GLOB VOLCANO_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# OpStream lives in src/web/ but is platform-agnostic — compile it into
# the test binary so the wire-format tests (test_opstream.cpp) run natively.
add_executable(volcano_tests ${VOLCANO_TEST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/web/OpStream.cpp
)
target_link_libraries(volcano_tests PRIVATE
    GTest::gtest_main
    volcano_backend volcano_render volcano_plot volcano_text volcano_encode
)

target_include_directories(volcano_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(volcano_tests)
