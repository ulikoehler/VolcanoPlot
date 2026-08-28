// volcano/core/PhysicalDevice.cpp
#include "volcano/core/PhysicalDevice.hpp"

#include <algorithm>
#include <ranges>

namespace volcano::core {

namespace {

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice dev, vk::SurfaceKHR surface) {
    QueueFamilyIndices idx;
    auto props = dev.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); ++i) {
        const auto& q = props[i];
        if (q.queueFlags & vk::QueueFlagBits::eGraphics) idx.graphics = i;
        if (q.queueFlags & vk::QueueFlagBits::eCompute)  idx.compute = i;
        if (q.queueFlags & vk::QueueFlagBits::eTransfer) idx.transfer = i;
        if (surface) {
            if (dev.getSurfaceSupportKHR(i, surface) == VK_TRUE) idx.present = i;
        }
    }
    // Fall back: transfer family may be the graphics family.
    if (!idx.transfer) idx.transfer = idx.graphics;
    if (!idx.compute)  idx.compute  = idx.graphics;
    return idx;
}

int scoreDevice(vk::PhysicalDevice dev) {
    auto props = dev.getProperties();
    int score = 0;
    if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) score += 1000;
    else if (props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) score += 100;
    score += static_cast<int>(props.limits.maxImageDimension2D);
    return score;
}

} // namespace

PhysicalDevice::PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface) {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) return;

    // Pick highest-scoring device with complete queue families.
    int bestScore = -1;
    for (auto dev : devices) {
        auto fam = findQueueFamilies(dev, surface);
        if (!fam.complete()) continue;
        int s = scoreDevice(dev);
        if (s > bestScore) {
            bestScore = s;
            physical_ = dev;
            families_ = fam;
        }
    }
    if (!physical_ && !devices.empty()) {
        physical_ = devices.front();
        families_ = findQueueFamilies(physical_, surface);
    }
}

vk::PhysicalDeviceProperties PhysicalDevice::properties() const {
    return physical_.getProperties();
}

vk::PhysicalDeviceMemoryProperties PhysicalDevice::memoryProperties() const {
    return physical_.getMemoryProperties();
}

std::vector<std::string> PhysicalDevice::supportedExtensions() const {
    auto props = physical_.enumerateDeviceExtensionProperties();
    std::vector<std::string> out;
    out.reserve(props.size());
    for (const auto& p : props) out.emplace_back(p.extensionName);
    return out;
}

bool PhysicalDevice::supportsExtension(std::string_view name) const {
    auto exts = supportedExtensions();
    return std::ranges::any_of(exts, [&](const auto& s){ return s == name; });
}

} // namespace volcano::core
