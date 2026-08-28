// volcano/core/CommandPool.hpp — command pool ownership
#pragma once

#include <vulkan/vulkan.hpp>

namespace volcano::core {

class Device;

class CommandPool {
public:
    CommandPool() = default;
    CommandPool(vk::Device device, uint32_t queueFamily,
                vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    ~CommandPool();

    CommandPool(CommandPool&&) noexcept = default;
    CommandPool& operator=(CommandPool&&) noexcept = default;
    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    [[nodiscard]] vk::CommandPool handle() const noexcept { return pool_.get(); }

private:
    vk::UniqueCommandPool pool_;
};

} // namespace volcano::core
