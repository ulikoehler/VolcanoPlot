// volcano/core/CommandBuffer.cpp
#include "volcano/core/CommandBuffer.hpp"

#include <stdexcept>

namespace volcano::core {

CommandBuffer::CommandBuffer(vk::Device device, vk::CommandPool pool, vk::CommandBufferLevel level) {
    vk::CommandBufferAllocateInfo ai{};
    ai.setCommandPool(pool)
       .setLevel(level)
       .setCommandBufferCount(1);
    auto cbs = device.allocateCommandBuffers(ai);
    buffer_ = cbs.front();
}

void CommandBuffer::begin(vk::CommandBufferUsageFlags flags) {
    vk::CommandBufferBeginInfo bi{};
    bi.setFlags(flags);
    buffer_.begin(bi);
}

void CommandBuffer::end() { buffer_.end(); }

void CommandBuffer::reset() { buffer_.reset(); }

// ---- OneTimeCommands ----

OneTimeCommands::OneTimeCommands(vk::Device device, vk::CommandPool pool, vk::Queue queue)
    : device_(device), pool_(pool), queue_(queue), cb_(device, pool) {
    cb_.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
}

OneTimeCommands::~OneTimeCommands() {
    cb_.end();
    vk::CommandBuffer cmd = cb_.handle();
    vk::SubmitInfo si{};
    si.setCommandBuffers(cmd);
    queue_.submit(si);
    queue_.waitIdle();
}

} // namespace volcano::core
