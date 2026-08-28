# Component: volcano_core
# Vulkan device/queue/command abstraction built on Vulkan-Hpp + VMA.

set(VOLCANO_CORE_SOURCES
    ${VOLCANO_ROOT}/src/core/Instance.cpp
    ${VOLCANO_ROOT}/src/core/PhysicalDevice.cpp
    ${VOLCANO_ROOT}/src/core/Device.cpp
    ${VOLCANO_ROOT}/src/core/Queue.cpp
    ${VOLCANO_ROOT}/src/core/CommandPool.cpp
    ${VOLCANO_ROOT}/src/core/CommandBuffer.cpp
    ${VOLCANO_ROOT}/src/core/Allocator.cpp
    ${VOLCANO_ROOT}/src/core/Buffer.cpp
    ${VOLCANO_ROOT}/src/core/Image.cpp
    ${VOLCANO_ROOT}/src/core/ShaderModule.cpp
    ${VOLCANO_ROOT}/src/core/DescriptorPool.cpp
    ${VOLCANO_ROOT}/src/core/PipelineCache.cpp
)

volcano_add_component(volcano_core
    SOURCES ${VOLCANO_CORE_SOURCES}
    PUBLIC_LINK Vulkan::Vulkan
    PRIVATE_LINK
    PUBLIC_INC include/volcano/core
    PRIVATE_INC ${vulkanmemoryallocator_SOURCE_DIR}/include
    PUBLIC_DEFS
        $<$<CONFIG:Debug>:$<$<BOOL:${VOLCANO_ENABLE_VALIDATION}>:VOLCANO_ENABLE_VALIDATION_LAYERS=1>>
)
