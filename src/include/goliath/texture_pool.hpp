#pragma once

#include <volk.h>

namespace engine::texture_pool {
    void update(uint32_t ix, VkImageView view, VkImageLayout, VkSampler sampler);
    void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout);
}
