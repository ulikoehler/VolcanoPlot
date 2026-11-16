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
    /// `ok` (optional) is set in the destructor: true once the
    /// submitted work's fence signaled. The wait is bounded — on a
    /// wedged queue the flag comes back false and callers that hand
    /// the result to the GPU must treat the frame as failed rather
    /// than trusting a half-written buffer.
    OneTimeCommands(vk::Device device, vk::CommandPool pool, vk::Queue queue,
                    bool* ok = nullptr);
    ~OneTimeCommands();

    OneTimeCommands(const OneTimeCommands&) = delete;
    OneTimeCommands& operator=(const OneTimeCommands&) = delete;

    [[nodiscard]] vk::CommandBuffer handle() const noexcept { return cb_.handle(); }

private:
    vk::Device device_;
    vk::CommandPool pool_;
    vk::Queue queue_;
    bool* ok_ = nullptr;
    CommandBuffer cb_;
};

} // namespace volcano::core
