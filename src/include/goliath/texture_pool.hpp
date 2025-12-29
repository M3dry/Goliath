#pragma once

#include <volk.h>

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
