#pragma once

#include <volk.h>

namespace engine::texture_pool {
    static constexpr uint32_t null_ix = (uint32_t)-1;

    void update(uint32_t ix, VkImageView view, VkImageLayout layout, VkSampler sampler);
    void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout);
}
