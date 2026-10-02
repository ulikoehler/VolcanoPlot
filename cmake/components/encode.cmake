# Component: volcano_encode
# GPU-side image encoding (PNG/WebP via compute shaders) + CPU fallback.

set(VOLCANO_ENCODE_SOURCES
    ${VOLCANO_ROOT}/src/encode/SaveImage.cpp
    ${VOLCANO_ROOT}/src/encode/ImageDecoder.cpp
    ${VOLCANO_ROOT}/src/encode/PngEncoder.cpp
    ${VOLCANO_ROOT}/src/encode/WebpEncoder.cpp
    ${VOLCANO_ROOT}/src/encode/ExtraEncoders.cpp
    ${VOLCANO_ROOT}/src/encode/MovieWriters.cpp
)

if(VOLCANO_GPU_ENCODE)
    list(APPEND VOLCANO_ENCODE_SOURCES
        ${VOLCANO_ROOT}/src/encode/GpuPngEncoder.cpp
        ${VOLCANO_ROOT}/src/encode/GpuYuvEncoders.cpp
    )
endif()

# Optional codec/format dependencies.
find_package(JPEG QUIET)
find_package(ZLIB QUIET)

set(_defs)
set(_pub_defs)
if(VOLCANO_GPU_ENCODE)
    # Public: Renderer.cpp, examples and dependents branch on it too.
    list(APPEND _pub_defs VOLCANO_GPU_ENCODE=1)
endif()
if(PNG_FOUND)
    list(APPEND _defs VOLCANO_HAS_LIBPNG=1)
endif()
if(WEBP_FOUND)
    list(APPEND _defs VOLCANO_HAS_LIBWEBP=1)
endif()
if(JPEG_FOUND)
    list(APPEND _defs VOLCANO_HAS_JPEG=1)
endif()
if(ZLIB_FOUND)
    list(APPEND _defs VOLCANO_HAS_ZLIB=1)
endif()

set(_priv_link)
if(PNG_FOUND)
    list(APPEND _priv_link PNG::PNG)
endif()
if(WEBP_FOUND)
    list(APPEND _priv_link PkgConfig::WEBP)
endif()
if(JPEG_FOUND)
    list(APPEND _priv_link JPEG::JPEG)
endif()
if(ZLIB_FOUND)
    list(APPEND _priv_link ZLIB::ZLIB)
endif()

volcano_add_component(volcano_encode
    SOURCES ${VOLCANO_ENCODE_SOURCES}
    PUBLIC_LINK volcano_core
    PRIVATE_LINK ${_priv_link}
    PUBLIC_INC include/volcano/encode
    PUBLIC_DEFS ${_pub_defs}
    PRIVATE_DEFS ${_defs}
)
