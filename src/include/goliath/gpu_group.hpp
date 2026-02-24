#pragma once

#include "goliath/buffer.hpp"
#include "goliath/transport2.hpp"

namespace engine {
    struct GPUGroup {
        Buffer data;
        transport2::ticket ticket;

        void destroy();
    };
}

namespace engine::gpu_group {
    void begin();

    // returns the offset from which data was written
    uint32_t upload(uint32_t data_size, void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, void*), void* ctx = nullptr);

    [[nodiscard]] GPUGroup end(bool priority, VkBufferUsageFlags usage_flags, VkPipelineStageFlags2 stage,
                               VkAccessFlagBits2 access);
}
