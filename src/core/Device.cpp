// volcano/core/Device.cpp
#include "volcano/core/Device.hpp"

#include <algorithm>
#include <ranges>
#include <set>

namespace volcano::core {

namespace {

std::vector<const char*> filterExtensions(const std::vector<std::string>& wanted,
                                          const std::vector<std::string>& available) {
    std::vector<const char*> out;
    for (const auto& w : wanted) {
        if (std::ranges::any_of(available, [&](const auto& s){ return s == w; })) {
            out.push_back(w.c_str());
        }
    }
    return out;
}

} // namespace

Device::Device(PhysicalDevice physical, const DeviceDesc& desc)
    : physical_(std::move(physical)) {

    auto availExts = physical_.supportedExtensions();

    // Always request swapchain if surface present.
    std::vector<std::string> wanted = desc.extensions;
    if (desc.hasSurface &&
        std::ranges::find(wanted, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == wanted.end()) {
        wanted.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    auto exts = filterExtensions(wanted, availExts);

    // Unique queue families
    std::set<uint32_t> families;
    const auto& fam = physical_.queueFamilies();
    families.insert(fam.graphics.value_or(0));
    families.insert(fam.compute.value_or(0));
    families.insert(fam.transfer.value_or(0));

    std::vector<vk::DeviceQueueCreateInfo> queueCis;
    float priority = 1.0f;
    for (uint32_t f : families) {
        vk::DeviceQueueCreateInfo qci{};
        qci.setQueueFamilyIndex(f)
           .setQueuePriorities(priority);
        queueCis.push_back(qci);
    }

    // Convert layer names to const char*
    std::vector<const char*> layers;
    for (const auto& l : desc.layers) layers.push_back(l.c_str());

    vk::DeviceCreateInfo ci{};
    ci.setQueueCreateInfos(queueCis)
       .setPEnabledExtensionNames(exts)
       .setPEnabledLayerNames(layers);

    // Use Features2 chain if provided, else basic features.
    if (desc.pNextChain) {
        ci.pNext = desc.pNextChain;
    } else {
        ci.pEnabledFeatures = &desc.features.features;
    }

    device_ = physical_.handle().createDeviceUnique(ci);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device_.get());

    graphicsQueue_ = device_->getQueue(fam.graphics.value_or(0), 0);
    computeQueue_  = device_->getQueue(fam.compute.value_or(0), 0);
    transferQueue_ = device_->getQueue(fam.transfer.value_or(0), 0);
}

Device::~Device() {
    if (device_) device_->waitIdle();
}

void Device::waitIdle() const {
    device_->waitIdle();
}

} // namespace volcano::core
