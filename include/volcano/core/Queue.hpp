// volcano/core/Queue.hpp — thin queue wrapper (mostly for documentation)
#pragma once

#include <vulkan/vulkan.hpp>

namespace volcano::core {

class Queue {
public:
    Queue() = default;
    explicit Queue(vk::Queue q) : queue_(q) {}
    [[nodiscard]] vk::Queue handle() const noexcept { return queue_; }
    void waitIdle() const { queue_.waitIdle(); }
private:
    vk::Queue queue_;
};

} // namespace volcano::core
