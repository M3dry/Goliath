#pragma once

#include "goliath/texture.hpp"

#include <volk.h>

namespace engine::texture_pool {
    static constexpr uint32_t null_ix = (uint32_t)-1;

    extern GPUImage default_texture;
    extern VkImageView default_texture_view;
    static constexpr VkImageLayout default_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    extern VkSampler default_sampler;
}

namespace engine {
    class TexturePool {
      public:
        VkDescriptorSetLayout set_layout;
        VkDescriptorSet set;

        TexturePool();
        TexturePool(uint32_t capacity);

        void destroy();

        void update(uint32_t ix, VkImageView view, VkImageLayout layout, VkSampler sampler);
        void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout) const;

        uint32_t get_capacity() const;
      private:
        VkDescriptorPool pool;
        uint32_t capacity;
    };
}
