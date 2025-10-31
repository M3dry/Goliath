#pragma once

#include <volk.h>

namespace engine::visbuffer {
    void init(VkImageMemoryBarrier2* barriers);
    void destroy();

    void barrier(VkImageMemoryBarrier2* barrier);

    VkImageView get_view();

    void bind(uint32_t binding);
}
