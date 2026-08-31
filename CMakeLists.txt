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
# The cycle exists among the shared variants regardless of whether static
# libs are also enabled, so we must block shared libs entirely.
if(VOLCANO_BUILD_SHARED_LIBS)
    message(FATAL_ERROR
        "VOLCANO_BUILD_SHARED_LIBS=ON is not supported because volcano_render "
        "and volcano_plot have a circular dependency (renderers use plot types "
        "like Camera3D/Transform2D; plots use renderers like LineRenderer/"
        "FillRenderer). CMake only allows cyclic dependencies among static "
        "libraries.\n\n"
        "Re-configure with shared libs disabled:\n"
        "  cmake -B build -DVOLCANO_BUILD_SHARED_LIBS=OFF\n"
        "If you have a stale cache from a previous configure, remove it first:\n"
        "  rm CMakeCache.txt  (in-source)  or  rm -rf build/  (out-of-source)")
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
    message(FATAL_ERROR
        "shaderc is required for runtime GLSL->SPIR-V shader compilation, "
        "but it was not found.\n\n"
        "Install shaderc development packages:\n"
        "  Debian/Ubuntu:  sudo apt-get install libshaderc-dev glslang-dev spirv-tools\n"
        "  Fedora/RHEL:    sudo dnf install shaderc-devel glslang-devel spirv-tools\n"
        "  Arch Linux:     sudo pacman -S shaderc glslang spirv-tools\n"
        "  macOS (Homebrew): brew install shaderc\n\n"
        "Or install the Vulkan SDK which includes shaderc:\n"
        "  https://vulkan.lunarg.com/sdk/home")
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
# A custom-built system GTest (e.g. in /usr/local/) may have been built with
# HDF5 or MPI support, pulling in unwanted transitive dependencies. Set
# -DVOLCANO_USE_FETCHCONTENT_GTEST=ON to always use a clean FetchContent GTest.
if(VOLCANO_BUILD_TESTS)
    option(VOLCANO_USE_FETCHCONTENT_GTEST
           "Always FetchContent GoogleTest instead of using system GTest" OFF)
    if(NOT VOLCANO_USE_FETCHCONTENT_GTEST)
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
    endif()
    # Some system packages (GTest, Vulkan, shaderc, SDL3, etc.) are built
    # with MPI support and transitively require -lmpi_cxx at link time.
    # We check for MPI references in multiple ways:
    #   1. Check pkg-config modules' Libs/Libs.private for MPI
    #   2. Check GTest library files for MPI symbols (readelf/nm)
    #   3. Check SDL3 (if found) for MPI references
    # The final check is done after the test target is created
    # (in tests/CMakeLists.txt) by inspecting the resolved link flags.
    find_package(MPI QUIET)
    set(VOLCANO_MPI_LINK "")
    if(MPI_FOUND)
        # MPI::MPI_CXX may not include -lmpi_cxx correctly on all systems.
        # Find the library directly to be sure.
        find_library(MPI_CXX_LIBRARY NAMES mpi_cxx)
        if(MPI_CXX_LIBRARY)
            set(VOLCANO_MPI_LINK ${MPI_CXX_LIBRARY})
            if(TARGET MPI::MPI_CXX)
                # Also link MPI::MPI_CXX for include paths and other flags.
                list(APPEND VOLCANO_MPI_LINK MPI::MPI_CXX)
            endif()
        elseif(TARGET MPI::MPI_CXX)
            set(VOLCANO_MPI_LINK MPI::MPI_CXX)
        endif()
    endif()

    # Early detection: check pkg-config modules for MPI references.
    set(_mpi_in_pkgconfig FALSE)
    foreach(_pc_mod shaderc libwebp freetype2 harfbuzz)
        if(PkgConfig_FOUND)
            execute_process(
                COMMAND pkg-config --libs --static ${_pc_mod}
                OUTPUT_VARIABLE _pc_libs
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_pc_libs MATCHES "mpi")
                set(_mpi_in_pkgconfig TRUE)
                message(STATUS "MPI reference found in pkg-config module: ${_pc_mod}")
                break()
            endif()
        endif()
    endforeach()

    # Early detection: check GTest library files for MPI or HDF5 references.
    # A custom-built GTest (e.g. in /usr/local/) may link HDF5, which in turn
    # pulls in MPI. In that case, fall back to FetchContent GTest.
    set(_mpi_in_gtest FALSE)
    set(_hdf5_in_gtest FALSE)
    foreach(_gtest_lib "${GTEST_LIBRARY}" "${GTEST_MAIN_LIBRARY}")
        if(NOT EXISTS "${_gtest_lib}")
            continue()
        endif()
        execute_process(
            COMMAND readelf -d "${_gtest_lib}"
            OUTPUT_VARIABLE _readelf_out ERROR_QUIET
        )
        if(_readelf_out MATCHES "mpi")
            set(_mpi_in_gtest TRUE)
        endif()
        if(_readelf_out MATCHES "hdf5")
            set(_hdf5_in_gtest TRUE)
        endif()
        if(_gtest_lib MATCHES "\\.a$")
            execute_process(
                COMMAND nm "${_gtest_lib}"
                OUTPUT_VARIABLE _nm_out ERROR_QUIET
            )
            if(_nm_out MATCHES "MPI_|mpi_cxx|PMPI_|MPIX_")
                set(_mpi_in_gtest TRUE)
            endif()
            if(_nm_out MATCHES "H5|hid_t|hdf5")
                set(_hdf5_in_gtest TRUE)
            endif()
        endif()
    endforeach()
    if(_hdf5_in_gtest AND NOT VOLCANO_USE_FETCHCONTENT_GTEST)
        message(STATUS
            "System GTest (${GTEST_LIBRARY}) links HDF5 (which pulls in MPI). "
            "Falling back to FetchContent GTest to avoid unwanted MPI dependency.")
        set(VOLCANO_USE_FETCHCONTENT_GTEST ON)
        # Clear the system GTest so FetchContent is used instead.
        set(GTest_FOUND FALSE)
        set(GTEST_LIBRARY "")
        set(GTEST_MAIN_LIBRARY "")
        set(GTEST_INCLUDE_DIR "")
    endif()

    # Early detection: check SDL3 for MPI.
    set(_mpi_in_sdl3 FALSE)
    if(TARGET SDL3::SDL3)
        get_target_property(_sdl3_loc SDL3::SDL3 IMPORTED_LOCATION)
        if(_sdl3_loc AND EXISTS "${_sdl3_loc}")
            execute_process(
                COMMAND readelf -d "${_sdl3_loc}"
                OUTPUT_VARIABLE _sdl3_readelf ERROR_QUIET
            )
            if(_sdl3_readelf MATCHES "mpi")
                set(_mpi_in_sdl3 TRUE)
            endif()
        endif()
    endif()

    set(VOLCANO_MPI_REQUIRED ${_mpi_in_pkgconfig} OR ${_mpi_in_gtest} OR ${_mpi_in_sdl3})
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
