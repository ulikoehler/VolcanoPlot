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
    ~Device() = default;

    Device(Device&&) noexcept = default;
    Device& operator=(Device&&) noexcept = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    /// A second Device sharing this one's Vulkan device and queues;
    /// the underlying objects die with the last holder.
    [[nodiscard]] Device share() const {
        Device s;
        s.impl_ = impl_;
        return s;
    }

    [[nodiscard]] vk::Device handle() const noexcept {
        return impl_ ? impl_->device.get() : vk::Device{};
    }
    [[nodiscard]] vk::PhysicalDevice physical() const noexcept { return impl_ ? impl_->physical.handle() : vk::PhysicalDevice{}; }
    [[nodiscard]] const PhysicalDevice& physicalDevice() const noexcept { return impl_->physical; }
    [[nodiscard]] uint32_t graphicsFamily() const noexcept { return impl_->physical.queueFamilies().graphics.value(); }
    [[nodiscard]] uint32_t computeFamily() const noexcept { return impl_->physical.queueFamilies().compute.value(); }
    [[nodiscard]] uint32_t transferFamily() const noexcept { return impl_->physical.queueFamilies().transfer.value(); }
    [[nodiscard]] vk::Queue graphicsQueue() const noexcept { return impl_ ? impl_->graphicsQueue : vk::Queue{}; }
    [[nodiscard]] vk::Queue computeQueue() const noexcept { return impl_ ? impl_->computeQueue : vk::Queue{}; }
    [[nodiscard]] vk::Queue transferQueue() const noexcept { return impl_ ? impl_->transferQueue : vk::Queue{}; }

    /// Wait for all queues to be idle.
    void waitIdle() const;

private:
    struct Impl {
        PhysicalDevice physical;
        vk::UniqueDevice device;
        vk::Queue graphicsQueue = nullptr;
        vk::Queue computeQueue = nullptr;
        vk::Queue transferQueue = nullptr;
        ~Impl() { if (device) device->waitIdle(); }
    };
    std::shared_ptr<Impl> impl_;
};

} // namespace volcano::core
