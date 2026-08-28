// volcano/core/Instance.cpp
#include "volcano/core/Instance.hpp"

#include <algorithm>
#include <format>
#include <iostream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_CPP_STORAGE

namespace volcano::core {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    using S = vk::DebugUtilsMessageSeverityFlagBitsEXT;
    auto sev = static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(severity);
    const char* tag = "VOLCANO-VK";
    if (sev & S::eError) std::cerr << std::format("[{}] ERROR: {}\n", tag, data->pMessage);
    else if (sev & S::eWarning) std::cerr << std::format("[{}] WARN:  {}\n", tag, data->pMessage);
    else if (sev & S::eInfo)    std::cerr << std::format("[{}] INFO:  {}\n", tag, data->pMessage);
    // Verbose suppressed.
    return VK_FALSE;
}

} // namespace

Instance::Instance(const InstanceDesc& desc) {
    // Loader — Vulkan-Hpp dynamic dispatcher.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    uint32_t apiVersion = VK_API_VERSION_1_3;
    if (vk::enumerateInstanceVersion(&apiVersion) != vk::Result::eSuccess) {
        apiVersion = VK_API_VERSION_1_3;
    }

    vk::ApplicationInfo app{};
    app.setPApplicationName(desc.applicationName.c_str())
        .setApplicationVersion(desc.applicationVersion)
        .setPEngineName(desc.engineName.c_str())
        .setEngineVersion(desc.engineVersion)
        .setApiVersion(apiVersion);

    // Gather extensions
    auto props = vk::enumerateInstanceExtensionProperties();
    std::vector<bool> have(props.size(), false);

    auto hasExt = [&](std::string_view name) {
        return std::ranges::any_of(props, [&](const auto& p) {
            return std::string_view{p.extensionName} == name;
        });
    };

    std::vector<const char*> exts;
    // Portability enumeration is needed for MoltenVK / some drivers.
    if (hasExt(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        enabledExtensions_.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    if (desc.enableValidation && hasExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        enabledExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    for (const auto& e : desc.extraExtensions) {
        if (hasExt(e)) {
            exts.push_back(e.c_str());
            enabledExtensions_.push_back(e);
        }
    }

    // Layers
    std::vector<const char*> layers;
    if (desc.enableValidation) {
        auto layerProps = vk::enumerateInstanceLayerProperties();
        bool hasLayer = std::ranges::any_of(layerProps, [](const auto& p) {
            return std::string_view{p.layerName} == "VK_LAYER_KHRONOS_validation";
        });
        if (hasLayer) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            validation_ = true;
        }
    }

    vk::InstanceCreateInfo ci{};
    ci.setPApplicationInfo(&app)
      .setPEnabledExtensionNames(exts)
      .setPEnabledLayerNames(layers);
    if (std::ranges::any_of(enabledExtensions_,
            [](const auto& s){ return s == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME; })) {
        ci.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }

    instance_ = vk::createInstanceUnique(ci);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_.get());

    if (validation_) {
        using Sev = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;
        vk::DebugUtilsMessengerCreateInfoEXT mci{};
        mci.setMessageSeverity(Sev::eError | Sev::eWarning | Sev::eInfo)
           .setMessageType(Type::eGeneral | Type::eValidation | Type::ePerformance)
           .setPfnUserCallback(debugCallback);
        messenger_ = instance_.createDebugUtilsMessengerEXTUnique(mci);
    }
}

Instance::~Instance() = default;

} // namespace volcano::core
