#include "goliath/texture_pool.hpp"
#include "texture_pool_.hpp"

#include "goliath/engine.hpp"

namespace engine::texture_pool {
    VkDescriptorPool pool;
    VkDescriptorSetLayout set_layout;
    VkDescriptorSet set;
    uint32_t texture_count;

    void init(uint32_t tex_count) {
        texture_count = tex_count;

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = texture_count;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

        vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = texture_count;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                                 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                 VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flgas_info{};
        binding_flgas_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        binding_flgas_info.bindingCount = 1;
        binding_flgas_info.pBindingFlags = &binding_flags;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.pNext = &binding_flgas_info;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout);

        VkDescriptorSetVariableDescriptorCountAllocateInfo count_info{};
        count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        count_info.descriptorSetCount = 1;
        count_info.pDescriptorCounts = &texture_count;

        VkDescriptorSetAllocateInfo set_info{};
        set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_info.pNext = &count_info;
        set_info.descriptorPool = pool;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &set_layout;
        vkAllocateDescriptorSets(device, &set_info, &set);
    }

    void destroy() {
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
    }

    void update(uint32_t ix, VkImageView view, VkImageLayout layout, VkSampler sampler) {
        VkDescriptorImageInfo image_info{};
        image_info.imageView = view;
        image_info.sampler = sampler;
        image_info.imageLayout = layout;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;
        write.dstBinding = 0;
        write.dstArrayElement = ix;
        write.dstSet = set;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void bind(VkPipelineBindPoint bind_point, VkPipelineLayout layout) {
        vkCmdBindDescriptorSets(get_cmd_buf(), bind_point, layout, 0, 1, &set, 0, nullptr);
    }
}
