#pragma once

#include "goliath/buffer.hpp"

namespace engine {
    struct GPUGroup {
        Buffer data;

        uint32_t acquired_texture_count;
        uint32_t* acquired_texture_gids;

        void destroy();
    };
}

namespace engine::gpu_group {
    void begin();

    // returns the offset from which data was written
    uint32_t upload(uint32_t texture_gid_count, uint32_t data_size,
                    void (*upload_ptr)(uint8_t*, uint32_t, uint32_t, uint32_t*, uint32_t, void*), void* ctx = nullptr);

    [[nodiscard]] GPUGroup end(VkBufferMemoryBarrier2* barrier, VkBufferUsageFlags usage_flags);
}
