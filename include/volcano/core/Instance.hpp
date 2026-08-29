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
class Instance {
public:
    Instance() = default;
    explicit Instance(const InstanceDesc& desc);
    ~Instance();

    Instance(Instance&&) noexcept = default;
    Instance& operator=(Instance&&) noexcept = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] vk::Instance handle() const noexcept { return instance_.get(); }
    [[nodiscard]] bool validationEnabled() const noexcept { return validation_; }

    /// Available instance extensions (queried at construction).
    [[nodiscard]] const std::vector<std::string>& enabledExtensions() const noexcept {
        return enabledExtensions_;
    }

private:
    vk::UniqueInstance instance_;
    vk::DebugUtilsMessengerEXT messenger_;  // raw handle, destroyed manually
    bool validation_ = false;
    std::vector<std::string> enabledExtensions_;
};

} // namespace volcano::core
