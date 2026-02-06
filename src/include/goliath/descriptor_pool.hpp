#pragma once

#include <cstdint>
#include <span>
#include <volk.h>

namespace engine::descriptor {
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
    class DescriptorSet {
      private:
        std::array<VkDescriptorSetLayoutBinding, sizeof...(Bindings)> bindings{};
        VkDescriptorSetLayoutCreateInfo info{};

      public:
        DescriptorSet(VkDescriptorSetLayoutCreateFlags flags = 0) {
            uint32_t i = 0;
            auto f = [&](descriptor::Binding binding) {
                bindings[i].binding = i;
                bindings[i].descriptorCount = binding.count;
                bindings[i].descriptorType = (VkDescriptorType)(binding.type);
                bindings[i].stageFlags = binding.stages;

                i++;
            };
            (f(Bindings), ...);

            info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            info.bindingCount = sizeof...(Bindings);
            info.pBindings = bindings.data();
            info.flags = flags;
        }

        operator VkDescriptorSetLayoutCreateInfo() {
            return info;
        }
    };
}

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

    VkDescriptorSetLayout create_layout(const VkDescriptorSetLayoutCreateInfo& info);
    void destroy_layout(VkDescriptorSetLayout layout);
}
