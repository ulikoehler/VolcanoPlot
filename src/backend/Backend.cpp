// volcano/backend/Backend.cpp — factory
#include "volcano/backend/Backend.hpp"

#ifdef VOLCANO_HAS_SCREEN_BACKEND
#include "volcano/backend/ScreenBackend.hpp"
#endif
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
#include "volcano/backend/HeadlessBackend.hpp"
#endif

namespace volcano::backend {

std::unique_ptr<IBackend> createScreenBackend(const BackendDesc& desc) {
#ifdef VOLCANO_HAS_SCREEN_BACKEND
    return std::make_unique<ScreenBackend>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

std::unique_ptr<IBackend> createHeadlessBackend(const BackendDesc& desc) {
#ifdef VOLCANO_HAS_HEADLESS_BACKEND
    return std::make_unique<HeadlessBackend>(desc);
#else
    (void)desc;
    return nullptr;
#endif
}

vk::Format findDepthFormat(vk::PhysicalDevice phys) {
    const vk::Format candidates[] = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };
    for (auto fmt : candidates) {
        auto props = phys.getFormatProperties(fmt);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return fmt;
        }
    }
    return vk::Format::eUndefined;
}

} // namespace volcano::backend
