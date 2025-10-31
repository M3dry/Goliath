#include "goliath/compute.hpp"
#include "engine_.hpp"
#include "goliath/engine.hpp"

#include <volk.h>

namespace engine {
    ComputePipelineBuilder::ComputePipelineBuilder() {
        *this = this->clear_descriptor_layout();
    }

    ComputePipelineBuilder&& ComputePipelineBuilder::clear_descriptor_layout() {
        _set_layout[0] = descriptor::empty_set;
        _set_layout[1] = descriptor::empty_set;
        _set_layout[2] = descriptor::empty_set;
        _set_layout[3] = descriptor::empty_set;
        return std::move(*this);
    }

    ComputePipeline::ComputePipeline(ComputePipelineBuilder&& builder)
        : _push_constant_size(builder._push_constant_size) {
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

        VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &_pipeline_layout));

        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage = VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = builder._shader,
            .pName = "main",
        };
        info.layout = _pipeline_layout;

        VK_CHECK(vkCreateComputePipelines(device, nullptr, 1, &info, nullptr, &_pipeline));
    }

    void ComputePipeline::bind() {
        vkCmdBindPipeline(get_cmd_buf(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    }

    void ComputePipeline::dispatch(DispatchParams params) {
        auto cmd_buf = get_cmd_buf();
        auto& descriptor_pool = get_frame_descriptor_pool();

        if (_push_constant_size != 0) {
            vkCmdPushConstants(cmd_buf, _pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, _push_constant_size,
                               params.push_constant);
        }

        for (uint32_t i = 0; i < 3; i++) {
            if (params.descriptor_indexes[i] == (uint64_t)-1) continue;

            descriptor_pool.bind_set(params.descriptor_indexes[i], cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     _pipeline_layout, i);
        }

        vkCmdDispatch(cmd_buf, params.group_count_x, params.group_count_y, params.group_count_z);

    }
    void ComputePipeline::destroy() {
        vkDestroyPipelineLayout(device, _pipeline_layout, nullptr);
        vkDestroyPipeline(device, _pipeline, nullptr);
    }
}
