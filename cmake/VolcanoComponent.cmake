# VolcanoComponent.cmake — helper to build shared+static variants of a component
#
# Usage in cmake/components/<name>.cmake:
#
#   volcano_add_component(volcano_core
#       SOURCES src/core/Device.cpp ...
#       PUBLIC_LINK Vulkan::Vulkan ...
#       PRIVATE_LINK ...
#       PUBLIC_INC include include/volcano/core
#       PRIVATE_INC src
#   )
#
# Creates volcano_core_shared / volcano_core_static (per options) and
# volcano_core / volcano::core ALIAS targets.

function(volcano_add_component name)
    set(options)
    set(oneValueArgs CXX_STANDARD)
    set(multiValueArgs SOURCES PUBLIC_LINK PRIVATE_LINK PUBLIC_INC PRIVATE_INC PUBLIC_DEFS PRIVATE_DEFS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_CXX_STANDARD)
        set(ARG_CXX_STANDARD 23)
    endif()

    set(_variants "")
    if(VOLCANO_BUILD_SHARED_LIBS)
        add_library(${name}_shared SHARED ${ARG_SOURCES})
        list(APPEND _variants ${name}_shared)
    endif()
    if(VOLCANO_BUILD_STATIC_LIBS)
        add_library(${name}_static STATIC ${ARG_SOURCES})
        list(APPEND _variants ${name}_static)
    endif()

    foreach(_tgt IN LISTS _variants)
        target_include_directories(${_tgt}
            PUBLIC
                $<BUILD_INTERFACE:${VOLCANO_ROOT}/include>
                $<BUILD_INTERFACE:${VOLCANO_ROOT}/include/volcano>
                $<INSTALL_INTERFACE:include>
                $<INSTALL_INTERFACE:include/volcano>
            PRIVATE
                ${VOLCANO_ROOT}/src
        )
        if(ARG_PUBLIC_INC)
            foreach(inc ${ARG_PUBLIC_INC})
                target_include_directories(${_tgt} PUBLIC
                    $<BUILD_INTERFACE:${VOLCANO_ROOT}/${inc}>
                    $<INSTALL_INTERFACE:${inc}>)
            endforeach()
        endif()
        if(ARG_PRIVATE_INC)
            target_include_directories(${_tgt} PRIVATE ${ARG_PRIVATE_INC})
        endif()

        if(ARG_PUBLIC_LINK)
            target_link_libraries(${_tgt} PUBLIC ${ARG_PUBLIC_LINK})
        endif()
        if(ARG_PRIVATE_LINK)
            target_link_libraries(${_tgt} PRIVATE ${ARG_PRIVATE_LINK})
        endif()

        if(ARG_PUBLIC_DEFS)
            target_compile_definitions(${_tgt} PUBLIC ${ARG_PUBLIC_DEFS})
        endif()
        if(ARG_PRIVATE_DEFS)
            target_compile_definitions(${_tgt} PRIVATE ${ARG_PRIVATE_DEFS})
        endif()

        set_target_properties(${_tgt} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_STANDARD ${ARG_CXX_STANDARD}
            CXX_STANDARD_REQUIRED ON
        )
    endforeach()

    # ALIAS targets
    if(VOLCANO_BUILD_SHARED_LIBS)
        add_library(${name} ALIAS ${name}_shared)
        string(REPLACE "volcano_" "volcano::" _ns ${name})
        add_library(${_ns} ALIAS ${name}_shared)
    elseif(VOLCANO_BUILD_STATIC_LIBS)
        add_library(${name} ALIAS ${name}_static)
        string(REPLACE "volcano_" "volcano::" _ns ${name})
        add_library(${_ns} ALIAS ${name}_static)
    endif()
endfunction()
