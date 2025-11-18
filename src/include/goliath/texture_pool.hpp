#pragma once

#include "goliath/texture.hpp"

#include <utility>
#include <volk.h>

namespace engine::texture_pool {
    static constexpr uint32_t null_ix = (uint32_t)-1;

    extern GPUImage default_texture;
    extern VkImageView default_texture_view;
    static constexpr VkImageLayout default_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    extern VkSampler default_sampler;

    void update(uint32_t ix, VkImageView view, VkImageLayout layout, VkSampler sampler);
    void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout);

    std::pair<uint32_t, uint32_t> alloc(uint32_t count);
    void free(std::pair<uint32_t, uint32_t> block);
}
