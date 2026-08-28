// volcano/core/CommandBuffer.hpp — RAII command buffer recorder
#pragma once

#include "volcano/core/CommandPool.hpp"

#include <vulkan/vulkan.hpp>

namespace volcano::core {

class Device;

class CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(vk::Device device, vk::CommandPool pool, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);

    [[nodiscard]] vk::CommandBuffer handle() const noexcept { return buffer_; }

    void begin(vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    void end();
    void reset();

private:
    vk::CommandBuffer buffer_; // owned by the pool
};

/// One-time command buffer + submit + wait helper.
class OneTimeCommands {
public:
    OneTimeCommands(vk::Device device, vk::CommandPool pool, vk::Queue queue);
    ~OneTimeCommands();

    OneTimeCommands(const OneTimeCommands&) = delete;
    OneTimeCommands& operator=(const OneTimeCommands&) = delete;

    [[nodiscard]] vk::CommandBuffer handle() const noexcept { return cb_.handle(); }

private:
    vk::Device device_;
    vk::CommandPool pool_;
    vk::Queue queue_;
    CommandBuffer cb_;
};

} // namespace volcano::core
