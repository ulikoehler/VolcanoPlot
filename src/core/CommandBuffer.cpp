// volcano/core/CommandBuffer.cpp
#include "volcano/core/CommandBuffer.hpp"

#include "volcano/core/Device.hpp"

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace volcano::core {

CommandBuffer::CommandBuffer(vk::Device device, vk::CommandPool pool, vk::CommandBufferLevel level) {
    vk::CommandBufferAllocateInfo ai{};
    ai.setCommandPool(pool)
       .setLevel(level)
       .setCommandBufferCount(1);
    // Command pools need external sync on alloc/free — backends share
    // pools under the shared-device context.
    std::lock_guard lk(globalQueueMutex());
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
    // Destructors must not throw — a failed submit (device lost,
    // OOM under contention) escaping here is std::terminate, and
    // when this dtor runs during unwind it's terminate *inside*
    // terminate. A skipped one-shot command degrades; an abort
    // kills the process.
    try {
        cb_.end();
        vk::CommandBuffer cmd = cb_.handle();
        vk::SubmitInfo si{};
        si.setCommandBuffers(cmd);
        vk::UniqueFence fence = device_.createFenceUnique({});
        {
            // Queue ops need external sync — shared-device backends
            // submit from different worker threads.
            std::lock_guard lk(globalQueueMutex());
            queue_.submit(si, fence.get());
        }
        // vkQueueWaitIdle/vkWaitForFences spin in the driver on some
        // stacks and bill the GPU execution time to this thread's CPU —
        // which is the dominant per-frame CPU cost for live plots (the
        // envelope readback runs this helper once per trace per frame).
        // Poll the fence status with a short sleep instead: identical
        // synchronous semantics, ~zero CPU. Bounded — a wedged queue
        // would otherwise pin this thread inside the destructor
        // forever; a skipped wait degrades (caller reads stale data)
        // but the process survives.
        for (uint32_t i = 0; i < 50000; ++i) {
            if (device_.getFenceStatus(fence.get()) != vk::Result::eNotReady)
                break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    } catch (...) {
    }
}

} // namespace volcano::core
