#pragma once

#include "goliath/engine.hpp"

#include <cstdint>
#include <span>

namespace engine::descriptor {
    static constexpr uint64_t null_set = (uint64_t)-1;
    extern VkDescriptorSetLayout empty_set;

    uint64_t new_set(VkDescriptorSetLayout layout);
    void bind_set(uint64_t id, VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t set);
    void update_set(uint64_t id, std::span<VkWriteDescriptorSet> writes);

    void begin_update(uint64_t id);
    void end_update();

    void update_ubo(uint32_t binding, std::span<uint8_t> ubo);
    void update_sampled_image(uint32_t binding, VkImageLayout layout, VkImageView view, VkSampler sampler);
    void update_storage_image(uint32_t binding, VkImageLayout layout, VkImageView view);

    struct Binding {
        enum Type {
            SampledImage = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            StorageImage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            StorageBuffer = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            UBO = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        };

        uint32_t count;
        Type type;
        VkShaderStageFlags stages;
    };
}

namespace engine {
    template <descriptor::Binding... Bindings>
    struct DescriptorSet {
        static VkDescriptorSetLayout create(VkDescriptorSetLayoutCreateFlags flags = 0) {
            VkDescriptorSetLayoutBinding bindings[sizeof...(Bindings)]{};

            uint32_t i = 0;
            auto f = [&](descriptor::Binding binding) {
                bindings[i].binding = i;
                bindings[i].descriptorCount = binding.count;
                bindings[i].descriptorType = (VkDescriptorType)(binding.type);
                bindings[i].stageFlags = binding.stages;

                i++;
            };
            (f(Bindings), ...);

            VkDescriptorSetLayoutCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            info.bindingCount = sizeof...(Bindings);
            info.pBindings = bindings;
            info.flags = flags;

            VkDescriptorSetLayout set_layout;
            vkCreateDescriptorSetLayout(device, &info, nullptr, &set_layout);
            return set_layout;
        }
    };

    void destroy_descriptor_set_layout(VkDescriptorSetLayout set_layout);
}
