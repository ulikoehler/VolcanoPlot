// volcano/core/PhysicalDevice.hpp — physical device selection
#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <string>
#include <vector>

namespace volcano::core {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> compute;
    std::optional<uint32_t> transfer;
    std::optional<uint32_t> present;

    [[nodiscard]] bool complete() const noexcept {
        return graphics.has_value() && compute.has_value() && transfer.has_value();
    }
};

class Instance;

/// Selects and wraps a vk::PhysicalDevice.
class PhysicalDevice {
public:
    PhysicalDevice() = default;
    PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface = nullptr);

    [[nodiscard]] vk::PhysicalDevice handle() const noexcept { return physical_; }
    [[nodiscard]] const QueueFamilyIndices& queueFamilies() const noexcept { return families_; }
    [[nodiscard]] vk::PhysicalDeviceProperties properties() const;
    [[nodiscard]] vk::PhysicalDeviceMemoryProperties memoryProperties() const;
    [[nodiscard]] std::vector<std::string> supportedExtensions() const;
    [[nodiscard]] bool supportsExtension(std::string_view name) const;

private:
    vk::PhysicalDevice physical_;
    QueueFamilyIndices families_;
};

} // namespace volcano::core
