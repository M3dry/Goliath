#pragma once

#include <volk.h>

namespace engine::texture_pool {
    extern VkDescriptorSetLayout set_layout;
    extern VkDescriptorSet set;

    void init(uint32_t tex_count);
    void destroy();
}
