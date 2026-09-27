// volcano/core/Instance.hpp — Vulkan instance abstraction (Vulkan-Hpp)
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include <memory>
#include <string>
#include <vector>

namespace volcano::core {

struct InstanceDesc {
    std::string applicationName = "VolcanoPlot";
    uint32_t applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    std::string engineName = "VolcanoPlot";
    uint32_t engineVersion = VK_MAKE_VERSION(0, 1, 0);
    bool enableValidation = false;
    std::vector<std::string> extraExtensions;
};

/// Owns a vk::Instance and (optionally) a debug messenger.
/// share() produces a handle whose underlying Vulkan objects are kept
/// alive by shared ownership — the last Instance destroys them.
class Instance {
public:
    Instance() = default;
    explicit Instance(const InstanceDesc& desc);
    ~Instance() = default;

    Instance(Instance&&) noexcept = default;
    Instance& operator=(Instance&&) noexcept = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    /// A second Instance sharing this one's Vulkan handles; the
    /// underlying objects die with the last holder.
    [[nodiscard]] Instance share() const {
        Instance s;
        s.impl_ = impl_;
        s.validation_ = validation_;
        s.enabledExtensions_ = enabledExtensions_;
        return s;
    }

    [[nodiscard]] vk::Instance handle() const noexcept {
        return impl_ ? impl_->instance.get() : vk::Instance{};
    }
    [[nodiscard]] bool validationEnabled() const noexcept { return validation_; }

    /// Available instance extensions (queried at construction).
    [[nodiscard]] const std::vector<std::string>& enabledExtensions() const noexcept {
        return enabledExtensions_;
    }

private:
    struct Impl {
        vk::UniqueInstance instance;
        vk::DebugUtilsMessengerEXT messenger = nullptr;
        ~Impl() {
            if (messenger && instance)
                instance.get().destroyDebugUtilsMessengerEXT(messenger);
        }
    };
    std::shared_ptr<Impl> impl_;
    bool validation_ = false;
    std::vector<std::string> enabledExtensions_;
};

} // namespace volcano::core
