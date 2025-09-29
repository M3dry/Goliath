#pragma once

#include <volk.h>

namespace engine::rendering {
    extern VkDescriptorSetLayout empty_set;

    void create_empty_set();
    void destroy_empty_set();
}
