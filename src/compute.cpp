#include "goliath/compute.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"
#include "include/goliath/compute.hpp"

#include <variant>
#include <volk.h>

namespace engine {
    ComputePipelineBuilder::ComputePipelineBuilder() {
        *this = this->clear_descriptor_layout();
    }

    ComputePipelineBuilder&& ComputePipelineBuilder::clear_descriptor_layout() {
        _set_layout[0] = empty_set();
        _set_layout[1] = empty_set();
        _set_layout[2] = empty_set();
        _set_layout[3] = empty_set();
        return std::move(*this);
    }

    void ComputePipeline::bind() const {
        vkCmdBindPipeline(get_cmd_buf(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    }

    void ComputePipeline::dispatch(const DispatchParams& params) const {
        auto cmd_buf = get_cmd_buf();
        auto& descriptor_pool = get_frame_descriptor_pool();

        if (_push_constant_size != 0) {
            vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, _push_constant_size,
                               params.push_constant);
        }

        for (uint32_t i = 0; i < 3; i++) {
            if (std::holds_alternative<uint64_t>(params.descriptor_indexes[i])) {
                auto ix = std::get<uint64_t>(params.descriptor_indexes[i]);
                if (ix == (uint64_t)-1) continue;

                descriptor_pool.bind_set(std::get<uint64_t>(params.descriptor_indexes[i]), cmd_buf,
                                         VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline_layout, i);
            } else {
                auto set = std::get<VkDescriptorSet>(params.descriptor_indexes[i]);
                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline_layout, i, 1, &set, 0,
                                        nullptr);
            }
        }

        vkCmdDispatch(cmd_buf, params.group_count_x, params.group_count_y, params.group_count_z);
    }

    void ComputePipeline::dispatch_indirect(const IndirectDispatchParams& params) const {
        auto cmd_buf = get_cmd_buf();
        auto& descriptor_pool = get_frame_descriptor_pool();

        if (_push_constant_size != 0) {
            vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, _push_constant_size,
                               params.push_constant);
        }

        for (uint32_t i = 0; i < 3; i++) {
            if (std::holds_alternative<uint64_t>(params.descriptor_indexes[i])) {
                auto ix = std::get<uint64_t>(params.descriptor_indexes[i]);
                if (ix == (uint64_t)-1) continue;

                descriptor_pool.bind_set(std::get<uint64_t>(params.descriptor_indexes[i]), cmd_buf,
                                         VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline_layout, i);
            } else {
                auto set = std::get<VkDescriptorSet>(params.descriptor_indexes[i]);
                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline_layout, i, 1, &set, 0,
                                        nullptr);
            }
        }

        vkCmdDispatchIndirect(cmd_buf, params.indirect_buffer, params.buffer_offset);
    }
}

namespace engine::compute {
    ComputePipeline create(const ComputePipelineBuilder& builder) {
        ComputePipeline res{};
        res._push_constant_size = builder._push_constant_size;

        VkPushConstantRange push_constant_range{};
        push_constant_range.size = builder._push_constant_size;
        push_constant_range.offset = 0;
        push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = builder._push_constant_size == 0 ? 0 : 1;
        layout_info.pPushConstantRanges = &push_constant_range;
        layout_info.setLayoutCount = 4;
        layout_info.pSetLayouts = builder._set_layout;

        VK_CHECK(vkCreatePipelineLayout(device(), &layout_info, nullptr, &res._pipeline_layout));

        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage = VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = builder._shader,
            .pName = "main",
        };
        info.layout = res._pipeline_layout;

        VK_CHECK(vkCreateComputePipelines(device(), nullptr, 1, &info, nullptr, &res._pipeline));

        return res;
    }

    void destroy(ComputePipeline& pipeline) {
        vkDestroyPipelineLayout(device(), pipeline._pipeline_layout, nullptr);
        vkDestroyPipeline(device(), pipeline._pipeline, nullptr);
    }
}
