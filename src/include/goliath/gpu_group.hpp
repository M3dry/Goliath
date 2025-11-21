#pragma once

#include "goliath/buffer.hpp"

namespace engine {
    struct GPUGroup {
        Buffer data;

        void destroy();
    };
}

namespace engine::gpu_group {
    void begin();

    // returns the offset from which data was written
    uint32_t upload(uint32_t data_size, void(*upload_ptr)(uint8_t*, uint32_t, uint32_t, void*), void* ctx = nullptr);

    [[nodiscard]] GPUGroup end(VkBufferMemoryBarrier2* barrier, VkBufferUsageFlags usage_flags);
}
