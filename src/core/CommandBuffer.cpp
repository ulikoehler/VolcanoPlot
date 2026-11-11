// volcano/core/CommandBuffer.cpp
#include "volcano/core/CommandBuffer.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

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
    vk::UniqueFence fence = device_.createFenceUnique({});
    queue_.submit(si, fence.get());
    // vkQueueWaitIdle/vkWaitForFences spin in the driver on some stacks
    // and bill the GPU execution time to this thread's CPU — which is
    // the dominant per-frame CPU cost for live plots (the envelope
    // readback runs this helper once per trace per frame). Poll the
    // fence status with a short sleep instead: identical synchronous
    // semantics, ~zero CPU.
    while (device_.getFenceStatus(fence.get()) == vk::Result::eNotReady) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

} // namespace volcano::core
