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

# After the target is fully linked, check if MPI is actually required.
# Two strategies are combined:
#   1. Inspect CMake link properties (INTERFACE_LINK_LIBRARIES, etc.) —
#      catches imported targets that explicitly list MPI deps.
#   2. Scan the actual library files with readelf/nm — catches static
#      libraries (e.g. libgtest.a) with undefined MPI symbols that CMake
#      doesn't know about (the find_library fallback path creates imported
#      targets without INTERFACE_LINK_LIBRARIES).
get_target_property(_test_link_libs volcano_tests LINK_LIBRARIES)
get_target_property(_test_iface_libs volcano_tests INTERFACE_LINK_LIBRARIES)
set(_all_link_libs "")
if(_test_link_libs)
    list(APPEND _all_link_libs ${_test_link_libs})
endif()
if(_test_iface_libs)
    list(APPEND _all_link_libs ${_test_iface_libs})
endif()

# Recursively resolve INTERFACE_LINK_LIBRARIES, INTERFACE_LINK_OPTIONS,
# and IMPORTED_LOCATION of all linked targets.
set(_resolved_libs ${_all_link_libs})
set(_queue ${_all_link_libs})
set(_lib_files "")
while(_queue)
    list(POP_FRONT _queue _item)
    if(TARGET ${_item})
        get_target_property(_item_iface ${_item} INTERFACE_LINK_LIBRARIES)
        if(_item_iface)
            foreach(_sub ${_item_iface})
                if(NOT _sub IN_LIST _resolved_libs)
                    list(APPEND _resolved_libs ${_sub})
                    list(APPEND _queue ${_sub})
                endif()
            endforeach()
        endif()
        get_target_property(_item_opts ${_item} INTERFACE_LINK_OPTIONS)
        if(_item_opts)
            foreach(_opt ${_item_opts})
                if(NOT _opt IN_LIST _resolved_libs)
                    list(APPEND _resolved_libs ${_opt})
                endif()
            endforeach()
        endif()
        foreach(_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE)
            get_target_property(_item_loc ${_item} ${_prop})
            if(_item_loc AND NOT _item_loc IN_LIST _resolved_libs)
                list(APPEND _resolved_libs ${_item_loc})
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
string(REPLACE ";" " " _link_str "${_resolved_libs}")
set(_mpi_in_cmake_props FALSE)
if(_link_str MATCHES "mpi|MPI")
    set(_mpi_in_cmake_props TRUE)
endif()

# Strategy 2: Scan actual library files for MPI references.
# Shared libs: readelf -d shows DT_NEEDED entries.
# Static libs: nm shows undefined symbols.
set(_mpi_in_lib_files FALSE)
set(_mpi_referring_lib "")
foreach(_lib IN LISTS _lib_files)
    if(NOT EXISTS "${_lib}")
        continue()
    endif()
    execute_process(
        COMMAND readelf -d "${_lib}"
        OUTPUT_VARIABLE _readelf_out
        ERROR_QUIET
        RESULT_VARIABLE _readelf_rc
    )
    if(_readelf_out MATCHES "mpi")
        set(_mpi_in_lib_files TRUE)
        set(_mpi_referring_lib "${_lib}")
        break()
    endif()
    if(_lib MATCHES "\\.a$")
        execute_process(
            COMMAND nm "${_lib}"
            OUTPUT_VARIABLE _nm_out
            ERROR_QUIET
            RESULT_VARIABLE _nm_rc
        )
        if(_nm_out MATCHES "MPI_|mpi_cxx|PMPI_|MPIX_")
            set(_mpi_in_lib_files TRUE)
            set(_mpi_referring_lib "${_lib}")
            break()
        endif()
    endif()
endforeach()

if(_mpi_in_cmake_props OR _mpi_in_lib_files)
    if(VOLCANO_MPI_LINK)
        message(STATUS "MPI required by system libraries — linked MPI::MPI_CXX")
    else()
        if(_mpi_referring_lib)
            set(_mpi_detail "Library: ${_mpi_referring_lib}")
        else()
            set(_mpi_detail "Link flags: ${_link_str}")
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
