#pragma once

#include <utility>
#include <volk.h>

namespace engine::texture_pool {
    static constexpr uint32_t null_ix = (uint32_t)-1;

    void update(uint32_t ix, VkImageView view, VkImageLayout layout, VkSampler sampler);
    void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout);

    std::pair<uint32_t, uint32_t> alloc(uint32_t count);
    void free(std::pair<uint32_t, uint32_t> block);
}
