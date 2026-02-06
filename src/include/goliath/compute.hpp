#pragma once

#include "goliath/descriptor_pool.hpp"
#include <array>
#include <variant>
#include <volk.h>

namespace engine {
    struct ComputePipelineBuilder {
        VkShaderModule _shader;

        VkDescriptorSetLayout _set_layout[4];
        uint32_t _push_constant_size = 0;

        ComputePipelineBuilder();

        ComputePipelineBuilder&& shader(VkShaderModule module) {
            _shader = module;
            return std::move(*this);
        }

        ComputePipelineBuilder&& push_constant(uint32_t size) {
            _push_constant_size = size;
            return std::move(*this);
        }

        ComputePipelineBuilder&& descriptor_layout(uint32_t index, VkDescriptorSetLayout layout) {
            _set_layout[index] = layout;
            return std::move(*this);
        }

        ComputePipelineBuilder&& clear_descriptor_layout();
    };

    struct ComputePipeline {
        using descriptor_set = std::variant<uint64_t, VkDescriptorSet>;
        struct DispatchParams {
            void* push_constant = nullptr;
            std::array<descriptor_set, 4> descriptor_indexes = {descriptor::null_set, descriptor::null_set,
                                                          descriptor::null_set};
            uint32_t group_count_x;
            uint32_t group_count_y;
            uint32_t group_count_z;
        };

        struct IndirectDispatchParams {
            void* push_constant = nullptr;
            std::array<descriptor_set, 4> descriptor_indexes = {descriptor::null_set, descriptor::null_set,
                                                          descriptor::null_set};
            VkBuffer indirect_buffer;
            uint64_t buffer_offset;
        };

        VkPipeline _pipeline;
        VkPipelineLayout _pipeline_layout;
        uint32_t _push_constant_size = 0;

        ComputePipeline() = default;

        void bind() const;
        void dispatch(const DispatchParams& params) const;
        void dispatch_indirect(const IndirectDispatchParams& params) const;
    };
}

namespace engine::compute {
    ComputePipeline create(const ComputePipelineBuilder& builder);
    void destroy(ComputePipeline& pipeline);
}
