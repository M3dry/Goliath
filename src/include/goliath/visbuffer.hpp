#pragma once

#include "goliath/rendering.hpp"
#include <volk.h>

namespace engine::visbuffer {
    void init(VkImageMemoryBarrier2* barriers);
    void destroy();

    void transition_to(VkImageMemoryBarrier2* _barrier, VkImageLayout layout);
    void barrier(VkImageMemoryBarrier2* barrier);

    VkImageView get_view();

    void bind(uint32_t binding);
    RenderingAttachement attach();
}
