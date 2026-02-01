#pragma once

#include <optional>
#include <span>
#include <volk.h>

namespace engine::synchronization {
    void begin_barriers();
    void end_barriers();

    void apply_barrier(VkImageMemoryBarrier2 barrier);
    void apply_barrier(VkBufferMemoryBarrier2 barrier);
    void apply_barrier(VkMemoryBarrier2 barrier);

    void submit_from_another_thread(std::span<VkBufferMemoryBarrier2> bufs, std::span<VkImageMemoryBarrier2> images,
                                    std::span<VkMemoryBarrier2> general, std::optional<VkSemaphoreSubmitInfo> wait_info,
                                    std::optional<VkSemaphoreSubmitInfo> signal_info);
}
