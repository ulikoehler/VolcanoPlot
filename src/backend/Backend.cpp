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

} // namespace volcano::backend
