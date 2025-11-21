#pragma once

#include <volk.h>

namespace engine::texture_pool {
    extern VkDescriptorSetLayout set_layout;
    extern VkDescriptorSet set;

    VkImageMemoryBarrier2  init_default_texture();
    void destroy_default_texture();
}
