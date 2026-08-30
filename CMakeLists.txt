# VolcanoPlot — Vulkan-based GPU plotter
#
# Modular CMakeLists.txt inspired by the Tether project layout.
# Components:
#   volcano_core:      Vulkan device/queue/command abstraction (Vulkan-Hpp + VMA)
#   volcano_backend:   Screen (SDL3) and headless offscreen backends
#   volcano_render:    Render passes, pipelines, MSAA, frame orchestration
#   volcano_plot:      Plot data model, axes, transforms, styles, plot types
#   volcano_encode:    GPU-side image encoding (PNG/WebP via compute) + CPU fallback
#   volcano_text:      SDF glyph atlas text rendering
#   volcano_cli:       Command-line frontends (screen liveplot, headless export)
#
# All components are built as both shared and static variants when enabled,
# with `volcano_<name>` and `volcano::<name>` ALIAS targets.

cmake_minimum_required(VERSION 3.22)
project(VolcanoPlot VERSION 0.1.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# C++ standard — modern, required.
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json for IDE / clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position-independent code for shared libs
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
# NOTE: volcano_render and volcano_plot have a circular dependency
# (renderers use plot types like Camera3D/Transform2D; plots use renderers
# like LineRenderer/FillRenderer). CMake only allows cyclic dependencies
# among static libraries, so shared libs are OFF by default.
option(VOLCANO_BUILD_SHARED_LIBS "Build shared library variants (disabled by default due to render<->plot cycle)" OFF)
option(VOLCANO_BUILD_STATIC_LIBS "Build static library variants" ON)
option(VOLCANO_BUILD_SCREEN_BACKEND "Build SDL3 screen backend" ON)
option(VOLCANO_BUILD_HEADLESS_BACKEND "Build headless offscreen backend" ON)
option(VOLCANO_BUILD_CLI "Build CLI applications" ON)
option(VOLCANO_BUILD_EXAMPLES "Build example applications" ON)
option(VOLCANO_BUILD_TESTS "Build tests (requires GoogleTest)" ON)
option(VOLCANO_ENABLE_VALIDATION "Enable Vulkan validation layers in debug" ON)
option(VOLCANO_GPU_ENCODE "Enable GPU-side image encoding (compute shaders)" ON)

# Parallelism guard — match Tether convention to avoid OOM on limited-RAM machines.
if(NOT DEFINED VOLCANO_BUILD_PARALLELISM)
    set(VOLCANO_BUILD_PARALLELISM 4)
endif()

# Guard: shared libs can't handle the render<->plot circular dependency.
if(VOLCANO_BUILD_SHARED_LIBS AND NOT VOLCANO_BUILD_STATIC_LIBS)
    message(FATAL_ERROR
        "VOLCANO_BUILD_SHARED_LIBS=ON is not supported because volcano_render "
        "and volcano_plot have a circular dependency (renderers use plot types "
        "like Camera3D/Transform2D; plots use renderers like LineRenderer/"
        "FillRenderer). CMake only allows cyclic dependencies among static "
        "libraries.\n\n"
        "Use: cmake -B build -DVOLCANO_BUILD_STATIC_LIBS=ON "
        "-DVOLCANO_BUILD_SHARED_LIBS=OFF")
endif()

# -----------------------------------------------------------------------------
# Helper: build both shared and static variants of a component.
# -----------------------------------------------------------------------------
# Defined in cmake/VolcanoComponent.cmake; included below.

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
# Vulkan loader + headers (system)
find_package(Vulkan REQUIRED)

# SDL3 (system) — for screen backend
if(VOLCANO_BUILD_SCREEN_BACKEND)
    find_package(SDL3 REQUIRED)
endif()

# shaderc (system) — runtime GLSL→SPIR-V compilation
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SHADERC IMPORTED_TARGET shaderc QUIET)
endif()
if(NOT SHADERC_FOUND)
    find_path(SHADERC_INCLUDE_DIR NAMES shaderc/shaderc.hpp)
    find_library(SHADERC_LIBRARY NAMES shaderc)
    if(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY)
        set(SHADERC_FOUND TRUE)
    endif()
endif()
if(NOT SHADERC_FOUND)
    message(WARNING "shaderc not found — runtime shader compilation disabled; SPIR-V must be precompiled.")
    set(VOLCANO_RUNTIME_SHADER_COMPILE OFF)
    set(VOLCANO_SHADERC_LINK "")
else()
    set(VOLCANO_RUNTIME_SHADER_COMPILE ON)
    if(TARGET PkgConfig::SHADERC)
        set(VOLCANO_SHADERC_LINK PkgConfig::SHADERC)
    else()
        set(VOLCANO_SHADERC_LINK ${SHADERC_LIBRARY})
    endif()
endif()

# libpng / libwebp — CPU fallback encoders
find_package(PNG QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(WEBP IMPORTED_TARGET libwebp QUIET)
endif()

# FreeType — font rasterization for text rendering
if(PkgConfig_FOUND)
    pkg_check_modules(FREETYPE IMPORTED_TARGET freetype2 QUIET)
endif()
if(NOT FREETYPE_FOUND)
    find_path(FREETYPE_INCLUDE_DIR NAMES ft2build.h PATH_SUFFIXES freetype2)
    find_library(FREETYPE_LIBRARY NAMES freetype)
    if(FREETYPE_INCLUDE_DIR AND FREETYPE_LIBRARY)
        set(FREETYPE_FOUND TRUE)
    endif()
endif()
if(FREETYPE_FOUND)
    set(VOLCANO_HAS_FREETYPE ON)
    if(TARGET PkgConfig::FREETYPE)
        set(VOLCANO_FREETYPE_LINK PkgConfig::FREETYPE)
    else()
        set(VOLCANO_FREETYPE_LINK ${FREETYPE_LIBRARY})
    endif()
    message(STATUS "FreeType found — text rendering enabled")
else()
    set(VOLCANO_HAS_FREETYPE OFF)
    set(VOLCANO_FREETYPE_LINK "")
    message(WARNING "FreeType not found — text rendering disabled")
endif()

# HarfBuzz — text shaping for glyb-based text rendering
if(PkgConfig_FOUND)
    pkg_check_modules(HARFBUZZ IMPORTED_TARGET harfbuzz QUIET)
endif()
if(NOT HARFBUZZ_FOUND)
    find_path(HARFBUZZ_INCLUDE_DIR NAMES hb.h)
    find_library(HARFBUZZ_LIBRARY NAMES harfbuzz)
    if(HARFBUZZ_INCLUDE_DIR AND HARFBUZZ_LIBRARY)
        set(HARFBUZZ_FOUND TRUE)
    endif()
endif()
if(HARFBUZZ_FOUND)
    set(VOLCANO_HAS_HARFBUZZ ON)
    if(TARGET PkgConfig::HARFBUZZ)
        set(VOLCANO_HARFBUZZ_LINK PkgConfig::HARFBUZZ)
    else()
        set(VOLCANO_HARFBUZZ_LINK ${HARFBUZZ_LIBRARY})
    endif()
    message(STATUS "HarfBuzz found — glyb text shaping enabled")
else()
    set(VOLCANO_HAS_HARFBUZZ OFF)
    set(VOLCANO_HARFBUZZ_LINK "")
    message(WARNING "HarfBuzz not found — glyb text shaping disabled")
endif()

# -----------------------------------------------------------------------------
# FetchContent: only for deps not available as system packages.
# Vulkan-Hpp is shipped with the Vulkan SDK / libvulkan-dev headers.
# GoogleTest is available as libgtest-dev on Debian/Ubuntu.
# VMA is pulled as a git submodule under dependencies/VulkanMemoryAllocator.
# -----------------------------------------------------------------------------
include(FetchContent)

# VulkanMemoryAllocator (VMA) — git submodule
set(VMA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/VulkanMemoryAllocator")
if(NOT EXISTS "${VMA_PATH}/include/vk_mem_alloc.h")
    message(FATAL_ERROR
        "Git submodule 'VulkanMemoryAllocator' is not initialized.\n"
        "Please run: git submodule update --init --recursive")
endif()

# GoogleTest — prefer system package, fall back to FetchContent.
if(VOLCANO_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        # Try manual detection (libgtest-dev ships headers + .so but no config)
        find_path(GTEST_INCLUDE_DIR NAMES gtest/gtest.h)
        find_library(GTEST_LIBRARY NAMES gtest)
        find_library(GTEST_MAIN_LIBRARY NAMES gtest_main)
        if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY AND GTEST_MAIN_LIBRARY)
            add_library(GTest::gtest UNKNOWN IMPORTED)
            set_target_properties(GTest::gtest PROPERTIES
                IMPORTED_LOCATION "${GTEST_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
            add_library(GTest::gtest_main UNKNOWN IMPORTED)
            set_target_properties(GTest::gtest_main PROPERTIES
                IMPORTED_LOCATION "${GTEST_MAIN_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
            set(GTest_FOUND TRUE)
        endif()
    endif()
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found on system; FetchContent'ing GoogleTest")
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
    include(GoogleTest)
endif()

# Use the dynamic dispatch loader so extension functions (debug messenger,
# swapchain, etc.) are resolved at runtime via vkGet*ProcAddr.
add_compile_definitions(VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
# The storage for the dynamic dispatcher must be defined in exactly one TU.
# We define it in Instance.cpp via VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE.
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/volcano
)

# VMA include (from submodule)
include_directories(${VMA_PATH}/include)

# glyb — git submodule (text rendering library)
set(GLYB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glyb")
if(NOT EXISTS "${GLYB_PATH}/src/glyph.h")
    message(FATAL_ERROR
        "Git submodule 'glyb' is not initialized.\n"
        "Please run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${GLYB_PATH}/third_party/glm/glm/glm.hpp")
    message(FATAL_ERROR
        "Git submodule 'glyb/third_party/glm' is not initialized.\n"
        "Please run: git submodule update --init --recursive dependencies/glyb")
endif()

# -----------------------------------------------------------------------------
# Component helper
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/VolcanoComponent.cmake)

# Root for component .cmake files
set(VOLCANO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# -----------------------------------------------------------------------------
# Components
# -----------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/core.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/backend.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/render.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/plot.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/encode.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/glyb.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components/text.cmake)

# -----------------------------------------------------------------------------
# CLI applications
# -----------------------------------------------------------------------------
if(VOLCANO_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

# -----------------------------------------------------------------------------
# Examples
# -----------------------------------------------------------------------------
if(VOLCANO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(VOLCANO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== VolcanoPlot ${PROJECT_VERSION} ===")
message(STATUS "  C++ standard:          ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:            ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs:           ${VOLCANO_BUILD_SHARED_LIBS}")
message(STATUS "  Static libs:           ${VOLCANO_BUILD_STATIC_LIBS}")
message(STATUS "  Screen backend (SDL3): ${VOLCANO_BUILD_SCREEN_BACKEND}")
message(STATUS "  Headless backend:      ${VOLCANO_BUILD_HEADLESS_BACKEND}")
message(STATUS "  GPU image encode:      ${VOLCANO_GPU_ENCODE}")
message(STATUS "  Runtime shader comp:   ${VOLCANO_RUNTIME_SHADER_COMPILE}")
message(STATUS "  Validation layers:     ${VOLCANO_ENABLE_VALIDATION}")
message(STATUS "  Tests:                 ${VOLCANO_BUILD_TESTS}")
message(STATUS "  Examples:              ${VOLCANO_BUILD_EXAMPLES}")
message(STATUS "=================================")
message(STATUS "")
