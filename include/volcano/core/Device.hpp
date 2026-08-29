// volcano/core/Device.hpp — logical device + queues
#pragma once

#include "volcano/core/PhysicalDevice.hpp"

#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

namespace volcano::core {

struct DeviceDesc {
    std::vector<std::string> extensions;
    std::vector<std::string> layers;
    vk::PhysicalDeviceFeatures2 features{};
    void* pNextChain = nullptr; // for feature chains (Vulkan 1.2+)
    bool hasSurface = true;     // if false, don't auto-add swapchain extension
};

class Device {
public:
    Device() = default;
    Device(PhysicalDevice physical, const DeviceDesc& desc);
    ~Device();

    Device(Device&&) noexcept = default;
    Device& operator=(Device&&) noexcept = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] vk::Device handle() const noexcept { return device_.get(); }
    [[nodiscard]] vk::PhysicalDevice physical() const noexcept { return physical_.handle(); }
    [[nodiscard]] const PhysicalDevice& physicalDevice() const noexcept { return physical_; }
    [[nodiscard]] uint32_t graphicsFamily() const noexcept { return physical_.queueFamilies().graphics.value(); }
    [[nodiscard]] uint32_t computeFamily() const noexcept { return physical_.queueFamilies().compute.value(); }
    [[nodiscard]] uint32_t transferFamily() const noexcept { return physical_.queueFamilies().transfer.value(); }
    [[nodiscard]] vk::Queue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] vk::Queue computeQueue() const noexcept { return computeQueue_; }
    [[nodiscard]] vk::Queue transferQueue() const noexcept { return transferQueue_; }

    /// Wait for all queues to be idle.
    void waitIdle() const;

private:
    PhysicalDevice physical_;
    vk::UniqueDevice device_;
    vk::Queue graphicsQueue_;
    vk::Queue computeQueue_;
    vk::Queue transferQueue_;
};

} // namespace volcano::core
