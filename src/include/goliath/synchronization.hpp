#pragma once

#include <volk.h>

namespace engine::synchronization {
    void begin_barriers();
    void end_barriers();

    void apply_barrier(VkImageMemoryBarrier2 barrier);
    void apply_barrier(VkBufferMemoryBarrier2 barrier);
    void apply_barrier(VkMemoryBarrier2 barrier);
}
