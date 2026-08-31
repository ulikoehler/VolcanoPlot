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

# Link MPI if it was found (some system libs transitively require -lmpi_cxx).
if(VOLCANO_MPI_LINK)
    target_link_libraries(volcano_tests PRIVATE ${VOLCANO_MPI_LINK})
endif()

target_include_directories(volcano_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Check if MPI is actually required by our dependencies.
# The early detection in CMakeLists.txt checks pkg-config modules, GTest
# library files, and SDL3. Here we also do a comprehensive check of the
# resolved link tree (CMake properties + library file scanning).
#
# If MPI is required but not installed → FATAL_ERROR with instructions.
# If MPI is required and installed → already linked via VOLCANO_MPI_LINK.

# Recursively resolve ALL link dependencies of the test target.
# Check BOTH INTERFACE_LINK_LIBRARIES (propagated) AND LINK_LIBRARIES
# (includes PRIVATE deps needed for static lib linking).
get_target_property(_test_link_libs volcano_tests LINK_LIBRARIES)
get_target_property(_test_iface_libs volcano_tests INTERFACE_LINK_LIBRARIES)
get_target_property(_test_link_opts volcano_tests LINK_OPTIONS)
set(_all_link_items "")
if(_test_link_libs)
    list(APPEND _all_link_items ${_test_link_libs})
endif()
if(_test_iface_libs)
    list(APPEND _all_link_items ${_test_iface_libs})
endif()
if(_test_link_opts)
    list(APPEND _all_link_items ${_test_link_opts})
endif()

set(_resolved_items ${_all_link_items})
set(_queue ${_all_link_items})
set(_lib_files "")
while(_queue)
    list(POP_FRONT _queue _item)
    if(TARGET ${_item})
        foreach(_prop INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
            get_target_property(_item_deps ${_item} ${_prop})
            if(_item_deps)
                foreach(_sub ${_item_deps})
                    if(NOT _sub IN_LIST _resolved_items)
                        list(APPEND _resolved_items ${_sub})
                        list(APPEND _queue ${_sub})
                    endif()
                endforeach()
            endif()
        endforeach()
        foreach(_prop INTERFACE_LINK_OPTIONS LINK_OPTIONS)
            get_target_property(_item_opts ${_item} ${_prop})
            if(_item_opts)
                foreach(_opt ${_item_opts})
                    if(NOT _opt IN_LIST _resolved_items)
                        list(APPEND _resolved_items ${_opt})
                    endif()
                endforeach()
            endif()
        endforeach()
        foreach(_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE
                      IMPORTED_LOCATION_DEBUG IMPORTED_LOCATION_NOCONFIG)
            get_target_property(_item_loc ${_item} ${_prop})
            if(_item_loc AND NOT _item_loc IN_LIST _resolved_items)
                list(APPEND _resolved_items ${_item_loc})
                if(EXISTS "${_item_loc}")
                    list(APPEND _lib_files "${_item_loc}")
                endif()
            endif()
        endforeach()
    elseif(EXISTS "${_item}")
        list(APPEND _lib_files "${_item}")
    endif()
endwhile()

# Strategy 1: Check CMake link properties for MPI references.
string(REPLACE ";" " " _link_str "${_resolved_items}")
set(_mpi_in_cmake_props FALSE)
if(_link_str MATCHES "mpi|MPI")
    set(_mpi_in_cmake_props TRUE)
endif()

# Strategy 2: Scan actual library files for MPI references.
set(_mpi_in_lib_files FALSE)
set(_mpi_referring_lib "")
foreach(_lib IN LISTS _lib_files)
    if(NOT EXISTS "${_lib}")
        continue()
    endif()
    execute_process(
        COMMAND readelf -d "${_lib}"
        OUTPUT_VARIABLE _readelf_out ERROR_QUIET
    )
    if(_readelf_out MATCHES "mpi")
        set(_mpi_in_lib_files TRUE)
        set(_mpi_referring_lib "${_lib}")
        break()
    endif()
    if(_lib MATCHES "\\.a$")
        execute_process(
            COMMAND nm "${_lib}"
            OUTPUT_VARIABLE _nm_out ERROR_QUIET
        )
        if(_nm_out MATCHES "MPI_|mpi_cxx|PMPI_|MPIX_")
            set(_mpi_in_lib_files TRUE)
            set(_mpi_referring_lib "${_lib}")
            break()
        endif()
    endif()
endforeach()

# Combine all detection strategies.
set(_mpi_required ${VOLCANO_MPI_REQUIRED})
if(_mpi_in_cmake_props OR _mpi_in_lib_files)
    set(_mpi_required TRUE)
endif()

if(_mpi_required)
    if(VOLCANO_MPI_LINK)
        message(STATUS "MPI required by system libraries — linked MPI::MPI_CXX")
    else()
        if(_mpi_referring_lib)
            set(_mpi_detail "Library: ${_mpi_referring_lib}")
        else()
            set(_mpi_detail "Detected via pkg-config or CMake link properties")
        endif()
        message(FATAL_ERROR
            "A system library transitively requires MPI (-lmpi_cxx), "
            "but MPI is not installed.\n\n"
            "${_mpi_detail}\n\n"
            "Install MPI development packages:\n"
            "  Debian/Ubuntu:  sudo apt-get install libopenmpi-dev\n"
            "  Fedora/RHEL:    sudo dnf install openmpi-devel\n"
            "  Arch Linux:     sudo pacman -S openmpi\n"
            "  macOS (Homebrew): brew install openmpi\n\n"
            "If you already have MPI installed but CMake can't find it, "
            "set CMAKE_PREFIX_PATH to its install location.")
    endif()
endif()

gtest_discover_tests(volcano_tests)
