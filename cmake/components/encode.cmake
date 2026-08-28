# Component: volcano_encode
# GPU-side image encoding (PNG/WebP via compute shaders) + CPU fallback.

set(VOLCANO_ENCODE_SOURCES
    ${VOLCANO_ROOT}/src/encode/ImageEncoder.cpp
    ${VOLCANO_ROOT}/src/encode/PngEncoder.cpp
    ${VOLCANO_ROOT}/src/encode/WebpEncoder.cpp
)

if(VOLCANO_GPU_ENCODE)
    list(APPEND VOLCANO_ENCODE_SOURCES
        ${VOLCANO_ROOT}/src/encode/GpuPngEncoder.cpp
    )
endif()

set(_defs)
if(VOLCANO_GPU_ENCODE)
    list(APPEND _defs VOLCANO_GPU_ENCODE=1)
endif()
if(PNG_FOUND)
    list(APPEND _defs VOLCANO_HAS_LIBPNG=1)
endif()
if(WEBP_FOUND)
    list(APPEND _defs VOLCANO_HAS_LIBWEBP=1)
endif()

set(_priv_link)
if(PNG_FOUND)
    list(APPEND _priv_link PNG::PNG)
endif()
if(WEBP_FOUND)
    list(APPEND _priv_link PkgConfig::WEBP)
endif()

volcano_add_component(volcano_encode
    SOURCES ${VOLCANO_ENCODE_SOURCES}
    PUBLIC_LINK volcano_core
    PRIVATE_LINK ${_priv_link}
    PUBLIC_INC include/volcano/encode
    PRIVATE_DEFS ${_defs}
)
